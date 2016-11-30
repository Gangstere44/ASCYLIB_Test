

#include "wait_free_stack2.h"

__thread ssmem_allocator_t* alloc_wf;

wf_stack_t* init_wf_stack(uint64_t num_thr) {

	wf_stack_t* new_stack = malloc(sizeof(wf_stack_t));
	
	new_stack->num_thr = num_thr;
	new_stack->sentinel.push_tid = -1; 
	new_stack->top = &new_stack->sentinel;
	
	new_stack->announces = calloc(num_thr, sizeof(push_op_t*));
	new_stack->handles = calloc(num_thr, sizeof(handle_t));
	uint64_t i;
	for(i = 0; i < num_thr; i++) {
		new_stack->announces[i] = NULL;
		
		new_stack->handles[i].ttd = i == num_thr - 1 ? 0 : i + 1;
	}
	
	new_stack->clean_tid = -1;

	return new_stack;
}

node_t* init_node(void* value, int64_t push_tid) {

	node_t* new_node = ssmem_alloc(alloc_wf, sizeof(*new_node));
	
	new_node->value = value;
	new_node->next_done = NULL;
	new_node->prev = NULL;
	new_node->mark = false;
	new_node->push_tid = push_tid;
	new_node->index = 0;

	return new_node;
}

push_op_t* init_push_op(node_t* n) {
	
	push_op_t* new_push_op = ssmem_alloc(alloc_wf, sizeof(*new_push_op));

	new_push_op->pushed = false;
	new_push_op->node = n;
	
	return new_push_op;	
}

uint64_t stack_size(wf_stack_t* s) {
	
	uint64_t n_elem = 0;
	node_t* tmp = s->top;

	volatile uint64_t rem = 0;
	
	while(tmp->push_tid != -1) {
		rem++;
		if(!tmp->mark) {
			n_elem++;
		}

		tmp = tmp->prev;	
	}
	
	printf("\n\n stack total : %lu \n\n", rem);

	return n_elem;
}

int64_t tid_to_help(wf_stack_t* s, int64_t tid) {

	int64_t to_help = s->handles[tid].ttd;
	s->handles[tid].ttd = (s->handles[tid].ttd + 1) % s->num_thr;
	if(s->handles[tid].ttd == tid) {
		s->handles[tid].ttd = (s->handles[tid].ttd + 1) % s->num_thr;
	}

	return to_help;
}

void push(wf_stack_t* s, int64_t tid, void* value) {
		//printf("push tid %ld \n", tid);

	node_t* new_node = init_node(value, tid);
	
	
	uint64_t i;
	for(i = 0 ; i < PATIENCE/4 ; i++) {
		
		if(push_fast(s, new_node)) {

			// try to help on of our peer
			int64_t to_help = tid_to_help(s, tid);
			push_slow(s, to_help);

			return;
		}
	}

	post_request(s, new_node, tid);
	push_slow(s, tid);
}

bool push_fast(wf_stack_t* s, node_t* n) {
	
	node_t* last = s->top;
	
	// now we can try to push our node, if it's not
	// already done
	if(last != n && n->index == 0 && CAS_U64_bool(&last->next_done, NULL, n)) {
		
		if(n->index > 0) {
			last->next_done = NULL;
		} else {

			if(CAS_U64_bool(&n->prev, NULL, last)) {
				n->index = last->index + 1;
				s->top = n;
			} else {
				last->next_done = NULL;
			}
		}

		return true;
	}
	
	return false;
}

void post_request(wf_stack_t* s, node_t* n, int64_t tid) {

	if(s->announces[tid] != NULL) {
		
		ssmem_free(alloc_wf, (void*) s->announces[tid]);
	}

	push_op_t* req = init_push_op(n);
	s->announces[tid] = req;
}

void push_slow(wf_stack_t* s, int64_t tid) {
	
	push_op_t* req = s->announces[tid];

	if(req != NULL && !req->pushed) {

		node_t* n = req->node;

		while(!req->pushed && !n->mark) {

			if(push_fast(s, n)) {
				req->pushed = true;
			}
		}
		
		if(!req->pushed) {
			if(CAS_U64_bool(&req->pushed, false, true) &&
				CAS_U64_bool(&n->prev, NULL, MARK_FOR_DEL)) {

				ssmem_free(alloc_wf, (void*) n);
			}
		} 
	}
}

void* pop(wf_stack_t* s, int64_t tid) {
	
	//printf("pop tid %ld \n", tid);

	node_t* cur = NULL;
	int64_t i;
	for(i = 0; i < PATIENCE; i++) {
		if(fast_pop(s, tid, &cur)) {
					
			try_clean_up(s, tid);

			return cur->value;
		}
	}
	
	slow_pop(s, tid, &cur);

	if(cur->push_tid == -1) {

		return EMPTY_STACK;
	}

	void* result = cur->value;

	try_clean_up(s, tid);

	return result;
}

bool fast_pop(wf_stack_t* s, int64_t tid, node_t** ret_n) {
	
	int64_t to_help = tid_to_help(s, tid);
	
	// try to get a slow push
	push_op_t* push_req = s->announces[to_help];
	
	if(push_req != NULL && !push_req->pushed) {
		
		bool mark = CAS_U64_bool(&push_req->node->mark, false, true);
	
		if(mark) {
			*ret_n = push_req->node;
			return true;
		}
	}

	// fast pop failed
	return false;
}

void slow_pop(wf_stack_t* s, int64_t tid, node_t** ret_n) {
	
	node_t* top = s->top;
	node_t* cur = top;

	while(cur->push_tid != -1) {

		bool mark = CAS_U64_bool(&cur->mark, false, true);

		if(mark) {
			break;
		}

		cur = cur->prev;

	}
	
	*ret_n = cur;
}

void try_clean_up(wf_stack_t* s, int64_t tid) {
	
	if(s->clean_tid != -1) {
		return;
	}
	
	if(CAS_U64_bool(&s->clean_tid, -1, tid)) {
		
		node_t* left = s->top;
		node_t* right = left->prev;
		
		int64_t cover_node = 0;
		while(right->push_tid != -1) {
			
			if(cover_node >= W) {
				cover_node = 0;
				clean(left, right);
				left->prev = right;
			}
			
			if(right->mark) {
				cover_node++;
			} else {
				left = right;
				cover_node = 0;
			}
			
			right = right->prev; 
		}
		
		s->clean_tid = -1;
	}
}

void clean(node_t* left, node_t* right) {
	
	left = left->prev; 
	node_t* tmp;
	while(left->index != right->index) {
		tmp = left;
		left = left->prev;
		ssmem_free(alloc_wf, (void*) tmp);
	}
}

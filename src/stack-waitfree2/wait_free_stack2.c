

#include "wait_free_stack.h"

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
		
		new_stack->handles.ttd = i == num_thr - 1 ? 0 : i + 1;
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
	new_node->counter = 0;

	return new_node;
}

push_op_t* init_push_op(uint64_t phase, node_t* n) {
	
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

void push(wf_stack_t* s, int64_t tid, void* value) {
	
	node_t* new_node = init_node(value, tid);
	
	uint64_t i;
	for(i = 0 ; i < PATIENCE ; i++) {
		
		if(push_fast(s, new_node, tid)) {
			return;
		}
	}
		
	push_slow(s, new_node, tid);
}

bool push_fast(wf_stack_t* s, node_t* n, int64_t tid) {
	
	node_t* last = s->top;
	
	if(CAS_U64_bool(&last->next_done, NULL, n)) {
		
		last = s->top;
		node_t* next = last->next_done;
		
		if(next == n) {
			
			n->prev = last;
			n->index = last->index + 1;
			
			bool stat = CAS_U64_bool(&s->top, last, next);
			
			CAS_U64_bool(&last->next_done, n, (void*) 1);		
		}
		
		return true;
	}
	
	return false;
}

void push_slow(wf_stack_t* s, node_t* n, int64_t tid) {
	
	if(s->announces[tid] != NULL) {
		
		if(s->announces[tid]->node->prev == NULL) {
			//ssmem_free()
		} 
		
		ssmem_free(alloc_wf, (void*) s->announces[tid]);
	}
	
	push_op_t* req = init_push_op(new_phase, new_node);
	s->announces[tid] = req;
	
	while(!req.pushed && !n.mark) {
		
		if(push_fast(s, n, tid)) {
			req->pushed = true;
		}
	}
	
	// make sure to say to my helper that my request is finally pushed
	req->pushed = true; 
}

node_t* pop(wf_stack_t* s, int64_t tid) {
	
	node_t* cur = NULL;
	int64_t i;
	for(i = 0; i < PATIENCE; i++) {
		if(fast_pop(s, tid, &cur)) {
			
			void* result = cur->value;
			
			return cur->value;
		}
	}
	
	slow_pop();

	

	if(cur->push_tid == -1) {

		return EMPTY_STACK;
	}

	void* result = cur->value;

	try_clean_up(s, tid);

	return result;
}

bool fast_pop(wf_stack_t* s, int64_t tid, node_t** ret_n) {
	
	int64_t to_help = s->handles[tid];
	s->handles[tid] = (s->handles[tid] + 1) % s->num_thr;
	if(s->handles[tid] == tid) {
		s->handles[tid] = (s->handles[tid] + 1) % s->num_thr;
	}
	
	push_op_t* push_req = s->announces[to_help];
	
	if(push_req != NULL && !push_req.pushed) {
		
		bool mark = CAS_U64_bool(&push_req->node->mark, false, true);
	
		if(mark) {
			*ret_n = push_req->node;
			return true;
		}
	}

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
	
	ret_n* = cur;
}

void try_clean_up(wf_stack_t* s, int64_t tid) {
	
	if(s->clean_tid != -1) {
		return;
	}
	
	if(CAS_U64_bool(&s->clean_tid, -1, tid)) {
		
		node_t* left = s->top;
		node_t* right = s->prev;
		
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
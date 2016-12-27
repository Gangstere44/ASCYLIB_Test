

#include "wait_free_stack2.h"

/********* PROFILING ********/

volatile ticks correction = 0;

#ifdef __tile__
#  include <arch/atomic.h>
#  define LFENCE arch_atomic_read_barrier()
#elif defined(__sparc__)
#  define LFENCE  asm volatile("membar #LoadLoad"); 
#else 
#  define LFENCE asm volatile ("lfence")
#endif


#  define START_TS()				\
    asm volatile ("");				\
    ticks start_acq = getticks();			\
    LFENCE;
#  define END_TS()				\
    asm volatile ("");				\
    ticks end_acq = getticks();			\
    asm volatile ("");
#  define ADD_DUR(tar) tar += (end_acq - start_acq - correction)

/******* *******/

#define min(val1, val2) (val1 > val2 ? val2 : val1)
#define max(val1, val2) (val1 > val2 ? val1 : val2)

__thread ssmem_allocator_t* alloc_wf;

wf_stack_t* init_wf_stack(uint64_t num_thr) {

/* +++++++ */
	  ticks t_dur = 0;
	  uint32_t j;
	  for (j = 0; j < 1000000; j++) {
	    ticks t_start = getticks();
	    ticks t_end = getticks();
	    t_dur += t_end - t_start;
	  }
	  correction = (ticks)(t_dur / (double) 1000000);

	/* +++++++ */ 

	wf_stack_t* new_stack = malloc(sizeof(wf_stack_t));
	
	new_stack->num_thr = num_thr;
	new_stack->sentinel.push_tid = -1; 
	new_stack->top = &new_stack->sentinel;
	
	new_stack->node_to_free = MIN_NODE_TO_FREE;

	new_stack->announces = calloc(num_thr, sizeof(push_op_t*));
	new_stack->handles = calloc(num_thr, sizeof(handle_t));
	uint64_t i;
	for(i = 0; i < num_thr; i++) {
		new_stack->announces[i] = NULL;
		
		new_stack->handles[i].ttd = i == num_thr - 1 ? 0 : i + 1;
	
		new_stack->handles[i].pop1_lat = 0;
		new_stack->handles[i].pop1_count = 0;
		new_stack->handles[i].pop2_lat = 0;
		new_stack->handles[i].pop2_count = 0;

		new_stack->handles[i].push_patience = MAX_PUSH_PATIENCE;
		new_stack->handles[i].pop_patience = MAX_POP_PATIENCE;
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
	s->handles[tid].push_patience = min(MAX_PUSH_PATIENCE, s->handles[tid].push_patience + 1);
	for(i = 0 ; i < s->handles[tid].push_patience; i++) {
		
		if(push_fast(s, new_node)) {
			// try to help on of our peer
			int64_t to_help = tid_to_help(s, tid);
			push_slow(s, to_help);

			return;
		}
	}
	
	s->handles[tid].push_patience = min(MAX_PUSH_PATIENCE, s->handles[tid].push_patience - 1);


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
	
	START_TS();

	node_t* cur = NULL;
	int64_t i;
	//s->handles[tid].pop_patience = min(MAX_POP_PATIENCE, s->handles[tid].pop_patience + 1);
	for(i = 0; i < s->handles[tid].pop_patience; i++) {
		if(fast_pop(s, tid, &cur)) {

			try_clean_up(s, tid);

			END_TS();
			ADD_DUR(s->handles[tid].pop1_lat);
			s->handles[tid].pop1_count++;

			return cur->value;
		}
	}

	//s->handles[tid].pop_patience = max(MIN_POP_PATIENCE, s->handles[tid].pop_patience * (2.0/3.0) - 1);
	
	slow_pop(s, tid, &cur);

	if(cur->push_tid == -1) {

		return EMPTY_STACK;
	}

	void* result = cur->value;

	try_clean_up(s, tid);

	END_TS();
	ADD_DUR(s->handles[tid].pop1_lat);
	s->handles[tid].pop1_count++;

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
	
	START_TS();

	if(CAS_U64_bool(&s->clean_tid, -1, tid)) {
		
		node_t* left = s->top;
		node_t* right = left->prev;
		
		uint64_t freed_node = 0;
		uint64_t i;
		for(i = 0; i < s->node_to_free && right->push_tid != -1; i++) {
			
			if(right->mark) {

				freed_node++;

				left->prev = right->prev;
				ssmem_free(alloc_wf, (void*) right);
				right = left->prev;
			} else {
				left = right;
				right = left->prev;
			}
		}

		if(freed_node <= (1/5.0) * s->node_to_free) {
			s->node_to_free = max(MIN_NODE_TO_FREE, s->node_to_free - 1);
		} else if(freed_node >= (4/5.0) * s->node_to_free) {
			s->node_to_free = min(MAX_NODE_TO_FREE, s->node_to_free + 1);
		}
		
		s->clean_tid = -1;
	}

	END_TS();
	ADD_DUR(s->handles[tid].pop2_lat);
	s->handles[tid].pop2_count++;
}

void print_profiling(wf_stack_t* s) {

	handle_t h = {0, 0, 0, 0, 0};

	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		h.pop1_lat += s->handles[i].pop1_lat;
		h.pop1_count += s->handles[i].pop1_count;

		h.pop2_lat += s->handles[i].pop2_lat;
		h.pop2_count += s->handles[i].pop2_count;
	}

	h.pop1_lat -= h.pop2_lat;
	h.pop1_lat /= h.pop1_count;

	h.pop2_lat /= h.pop2_count;

	printf("/***** LATENCY *****/ \n");
	printf("Pop1 : \t %lu  \nPop2 : \t %lu \n", h.pop1_lat, h.pop2_lat);
	printf("/*******************/\n");

	printf("Per thread result : \n");
	for(i = 0; i < s->num_thr; i++ ) {
		printf("Thread %lu : \n\tpush pat : %lu\n\tpop pat : %lu\n", i, s->handles[i].push_patience, s->handles[i].pop_patience);
	}
	printf("**** Gen node to free : %lu ****\n",s->node_to_free);
}
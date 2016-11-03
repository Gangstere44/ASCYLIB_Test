

#include "wait_free_stack.h"

__thread ssmem_allocator_t* alloc_wf;


int mark_bits(void* ptr) {

	return (((uint64_t) ptr) & 0x00000003);
}


wf_stack_t* init_wf_stack(uint64_t num_thr) {
	
	wf_stack_t* new_stack = malloc(sizeof(wf_stack_t));
	
	new_stack->num_thr = num_thr;
	new_stack->sentinel.push_tid = -1; 
	new_stack->top = &new_stack->sentinel;
	
	new_stack->announces = calloc(num_thr, sizeof(push_op_t*));
	new_stack->all_delete_requests = calloc(num_thr, sizeof(delete_req_t*));
	uint64_t i;
	for(i = 0; i < num_thr; i++) {
		new_stack->announces[i] = NULL;
		new_stack->all_delete_requests[i] = NULL;
	}
	
	new_stack->phase_counter_push_req = 0;
	new_stack->phase_counter_del_req = 0;

	//new_stack->unique_req = NULL;

	new_stack->unique_req = init_delete_req(0, -1, NULL);
	new_stack->unique_req->pending = false;
	
	return new_stack;
}

node_t* init_node(void* value, int64_t push_tid) {

	node_t* new_node = ssmem_alloc(alloc_wf, sizeof(node_t));
	
	new_node->value = value;
	new_node->next_done = NULL;
	new_node->prev = NULL;
	new_node->mark = false;
	new_node->push_tid = push_tid;
	new_node->index = 0;
	new_node->counter = 0;
	
	new_node->bob = 0;
	new_node->sam = 0;

	return new_node;
}

push_op_t* init_push_op(uint64_t phase, node_t* n) {
	
	push_op_t* new_push_op = ssmem_alloc(alloc_wf, sizeof(push_op_t));
	//push_op_t* new_push_op = malloc(sizeof(push_op_t));
	new_push_op->phase = phase;
	new_push_op->pushed = false;
	new_push_op->node = n;
	
	return new_push_op;	
}

delete_req_t* init_delete_req(uint64_t phase, int64_t tid, node_t* n) {
	
	//delete_req_t* new_del_req = ssmem_alloc(alloc_wf, sizeof(delete_req_t));
	delete_req_t* new_del_req = malloc(sizeof(delete_req_t));
	new_del_req->phase = phase;
	new_del_req->tid = tid;
	new_del_req->pending = true;
	new_del_req->node = n;

	return new_del_req;	
}

uint64_t stack_size(wf_stack_t* s) {
	
printf("stack size \n");
	uint64_t n_elem = 0;
	node_t* tmp = s->top;

volatile 	uint64_t rem = 0;
	
	while(tmp->push_tid != -1) {
		rem++;
		if(!tmp->mark) {
			n_elem++;
		}
	/*	
		printf("tmp->index %lu, tmp->mark %d, tmp->counter %lu, tmp->bob %lu, tmp->sam %lu\n", tmp->index, tmp->mark, tmp->counter, tmp->bob, tmp->sam);
		getchar();
*/
		//printf("stack 1 %p \n", tmp);
		tmp = tmp->prev;
		//printf("stack 2 %p \n", tmp);
		//printf("stack 3 %ld \n", tmp->push_tid);

		
	}
	
	printf("\n\n stack total : %lu \n\n", rem);

	return n_elem;
}

void push(wf_stack_t* s, int64_t tid, void* value) {

	uint64_t c = s->phase_counter_push_req;
	uint64_t mod = 10000000;

	if(c % mod == 0) 
	{

	}
	//printf("push 1 tid %ld\n", tid);

	uint64_t new_phase = FAI_U64(&s->phase_counter_push_req);

	if(s->announces[tid] != NULL) {

		//printf("1 \n");
		//printf(" phase %ld, index %lu \n", s->announces[tid]->phase, s->announces[tid]->node->index);
		//printf("2 \n");
		s->announces[tid]->node = NULL;
	//	ssmem_free(alloc_wf, (void*) s->announces[tid]);
		//free(s->announces[tid]);
	}

	push_op_t* new_op = init_push_op(new_phase, init_node(value, tid));
	s->announces[tid] = new_op;

	if(c % mod == 0) {

	}

	//printf("push 2 tid %ld\n", tid);


	help(s, s->announces[tid], tid);

	if(c % mod == 0) {

	}

	//	printf("push 3 tid %ld\n", tid);


}

void help(wf_stack_t* s, push_op_t* request, int64_t tid) {
	
	//printf("help 1 \n");

	push_op_t* min_req = NULL;
	
	bool first = true;
	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		
		push_op_t* tmp = s->announces[i];
		if(tmp != NULL && !tmp->pushed && 
			(first || (min_req != NULL && min_req->phase > tmp->phase))) {
			first = false;
			min_req = tmp;
		}
	}

	//	printf("help 2\n");


	if(min_req == NULL || min_req->phase > request->phase) {
		return;
	}
//	printf("help 3 \n");

	attach_node(s, min_req, tid);
//	printf("help 4 \n");

	if(min_req != request) {
	//		printf("help 5 \n");

		attach_node(s, request, tid);
	//		printf("help 6 \n");

	}
	//	printf("help 7 \n");


}

void attach_node(wf_stack_t* s, push_op_t* request, int64_t tid) {

	//printf("attach node 1 \n");
	volatile uint64_t i = 0;
	volatile uint64_t mod = 1000000;
	while(!request->pushed) {
	//printf("attach node 2 \n");
i++;
		node_t* last = s->top;

		if(i % mod == 0) {
			printf("blocked in attach node, top index %lu, request phase %lu\n", last->index, request->phase);
		}

		// can be 0x000000000, a ptr, a ptr with 1 in LSB, or only 1
		node_t* next = last->next_done;
		bool done = mark_bits(next);
				

		if(last == s->top) {

			if(next == NULL && !done) {

				if(!request->pushed) {

					node_t* node_req = request->node;
	//printf("attach node 4 \n");

					if(CAS_U64_bool(&last->next_done, NULL, node_req)) {
	//printf("attach node 5 \n");
	
						

						update_top(s, tid);
						CAS_U64_bool(&last->next_done, node_req, (void*) 1);
	//printf("attach node 6 \n");
		
						return;
					}
				}
			}
		//		printf("attach node 7 \n");

			update_top(s, tid);
		//		printf("attach node 8 \n");

		}
	}
}

void update_top(wf_stack_t* s, int64_t tid) {
	//printf("update 1 tid %lu \n", tid);
	//printf("0 tid %ld %p\n", tid, s->top);
	node_t* last = s->top;
//	printf("1 tid %ld %p\n", tid, last->next_done);
	node_t* next = last->next_done;

	if(((uint64_t) next) > 1) {
	//	printf("2 tid %ld \n", tid);
	//			printf("7 tid %ld %p \n", tid, next);

		push_op_t* request = s->announces[next->push_tid];
	//	printf("8 tid %ld \n", tid);
	//	printf("3 tid %ld %p\n", tid, request->node);
		
		if(last == s->top && request->node == next) {
		//	printf("4 tid %ld \n", tid);
		//	printf("5 tid %ld %p\n", tid, next->prev);
			CAS_U64_bool(&next->prev, NULL, last); 
		
			next->index = last->index + 1;
			request->pushed = true;
			bool stat = CAS_U64_bool(&s->top, last, next);

		
			if(next->index % W == 0 && stat) {
				next->bob++;
				try_clean_up(s, next, tid, true);
			}
		}
	}

	//printf("update 2 tid %lu \n", tid);
}

node_t* pop(wf_stack_t* s, int64_t tid) {

	uint64_t c = s->phase_counter_push_req;
	uint64_t mod = 1000000;

	if(c % mod == 0) 
{}
//	printf("pop 1 tid %ld\n", tid);



	node_t* top = s->top;
	node_t* cur = top;
	//	printf("pop 2 tid %ld\n", tid);

	while(cur->push_tid != -1) {
		


		bool mark = CAS_U64_bool(&cur->mark, false, true);
		
		if(mark) {
			break;
		}
		//	printf("pop 3 tid %ld, and  %p\n", tid, cur);

		cur = cur->prev;
		//	printf("pop 4 tid %ld, and %p\n", tid, cur);
		//		printf("pop bis tid %ld, next cur tid %ld\n", tid, cur->push_tid);


	}
	if(c % mod == 0) {}

	//printf("pop 5 tid %ld\n", tid);
	
	if(cur->push_tid == -1) {

		return EMPTY_STACK;
	}

		if(c % mod == 0) {}
	//printf("pop 6 tid %ld\n", tid);
	
	try_clean_up(s, cur, tid, false);

	//printf("pop 4\n");
	
	return cur->value;
}

void try_clean_up(wf_stack_t* s, node_t* n, int64_t tid, bool from_right_node) {

//	printf("try clean 1 \n");
//
	node_t* tmp = NULL;

	if(from_right_node) {
		tmp = n->prev;
	} else {
		tmp = n;
	}

	volatile uint64_t i = 0;
	volatile uint64_t mod = 1000000;
		while(tmp->push_tid != -1) {

			i++;
		if(i % mod == 0) {
		printf("blocked try clean up, tmp->index %lu, tmp->next->index %lu\n", tmp->index, tmp->prev->index);
	}

	//		printf("try clean 2 \n");

		if(tmp->index % W == 0) {
/*
			if(tmp->counter == W) {
				printf("Sth should happen, index %lu \n", tmp->index);
			}
			if(tmp->counter >= W + 1) {
				printf("WRONG \n");
			}
			*/
			if(IAF_U64(&tmp->counter) == W + 1) {
			//	printf("sth happens, index %lu \n", tmp->index);
			//	tmp->sam += 44;
	//			printf("try cl 3 \n");
				clean(s, tid, tmp); 
	//			printf("try cl 4 \n");
			} 
			break;
		}
	//		printf("try clean 5 \n");
	//		printf("tmp index %lu \n", tmp->index);
		tmp = tmp->prev;
	//		printf("try clean 6 \n");

			tmp->prev;
	//		printf("try clean 7 \n");
	}

	//	printf("try clean 8 \n");

	
}

void help_finish_delete(wf_stack_t* s) {
	
	delete_req_t* cur = s->unique_req;
	
	if(!cur->pending) {
		return;
	}
	
	uint64_t end_idx = cur->node->index + W - 1;
	node_t* right_node = s->top;
	node_t* left_node = right_node->prev;
			
				volatile uint64_t g = 0;
	volatile uint64_t mod = 1000000;
	while(left_node->index != end_idx && left_node->push_tid != -1) {
		g++;
		if(g % mod == 0) {
		printf("block help finish, left index %lu, right index %lu\n", left_node->index, right_node->index);
	}
		right_node = left_node;
		left_node = left_node->prev;
	}
	
	if(left_node->push_tid == -1) {
		return;
	}
	
	node_t* target = left_node;
	uint64_t i;
	for(i = 0; i < W; i++) {
			//	printf("taget->index %lu, target->counter %lu, marked %d , bob %d, sam %d \n", target->index, target->counter, target->mark, target->bob, target->sam);

		target = target->prev;
	}
			//		printf("\nrange %lu - %lu cleaned \n\n", target->index+1, left_node->index);

	CAS_U64_bool(&right_node->prev, left_node, target);

			
	
	node_t* tmp;
	for(i = 0; i < W; i++) {
		tmp = left_node;
		left_node = left_node->prev;
		//tmp->prev = NULL;
		//ssmem_free(alloc_wf, (void*) tmp);
	}
	
	cur->pending = false;
}

void clean(wf_stack_t* s, int64_t tid, node_t* n) {

//	printf("clean 1 \n");

	//printf("clean called");

	if(s->unique_req != s->all_delete_requests[tid] && s->all_delete_requests[tid] != NULL) {
		//ssmem_free(alloc_wf, (void*) s->all_delete_requests[tid]);
	}
	

	uint64_t phase = FAI_U64(&s->phase_counter_del_req);
	
	//printf("clean called with phase %lu \n", phase);

	s->all_delete_requests[tid] = init_delete_req(phase, tid, n);
	
//	printf("clean 2 \n");


	help_delete(s, s->all_delete_requests[tid]);

//	printf("clean 3 \n");

}

void help_delete(wf_stack_t* s, delete_req_t* dr) {
	
//	printf("help delete 1 \n");

	delete_req_t* min_req = NULL;
	
	bool first = true;
	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		
		delete_req_t* tmp = s->all_delete_requests[i];
		if(tmp != NULL && tmp->pending && 
			(first || (min_req != NULL && min_req->phase > tmp->phase))) {
			first = false;
			min_req = tmp;
		}
	}
	
	//printf("help delete 2 \n");


	if(min_req == NULL || min_req->phase > dr->phase) {
		return;
	}
	

	//printf("before unique_delete min_req: tid %i, pending %i, phase %ld \n", min_req->tid, min_req->pending, min_req->phase);
	//printf("before unique_delete unique_req: tid %i, pending %i, phase %ld \n", s->unique_req->tid, s->unique_req->pending, s->unique_req->phase);
	unique_delete(s, min_req);
	
	//printf("help delete 3 \n");


	if(min_req != dr) {
	//printf("help delete 4 \n");

		unique_delete(s, dr);
	//printf("help delete 5 \n");

	}
	//printf("help delete 6 \n");

}

void unique_delete(wf_stack_t* s, delete_req_t* dr) {
	//printf("unique_delete dr : %p, phase %lu \n", dr, dr->phase);
	volatile uint64_t i = 0;
	volatile uint64_t mod = 1000000;
		

	while(dr->pending) {
		i++;
//getchar();
//		printf("1\n");
		delete_req_t* cur = s->unique_req;
		//printf("(2)\n" );

	if(i % mod == 0) {
		printf("blocked uniq del, dr->phase %lu\n", dr->phase);
	}		

	//	printf("turn %lu \n", i++);

	//	printf("dr pending  = %i, dr node = %p , dr tid = %ld, dr phase %lu \n", dr->pending, dr->node->value, dr->tid, dr->phase);
	//	if(cur->tid != -1)
	//		printf("unique_req pending = %i, unique_req tid = %ld, unique req phase %lu \n", cur->pending, cur->tid, cur->phase);
		if(cur == NULL || !cur->pending) {
				//	printf("(3\n" );

			if(dr->pending) {
				//		printf("(4)\n" );

			//	printf("before state cur phase : %lu, dr phase : %lu\n", cur->phase, dr->phase);
				bool stat = dr != cur ? CAS_U64_bool(&s->unique_req, cur, dr) : true;

				help_finish_delete(s);

				if(stat) {

			//		free(cur);
			//		printf("good phase = %lu\n", dr->phase);
					
					return;
				} 
			}
		} else {
			help_finish_delete(s);
		}
	}
	
}


#include "stack-waitfree.h"

__thread ssmem_allocator_t* alloc_wf;

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

	new_stack->unique_req = NULL;

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

	new_push_op->phase = phase;
	new_push_op->pushed = false;
	new_push_op->node = n;
	
	return new_push_op;	
}

delete_req_t* init_delete_req(uint64_t phase, int64_t tid, node_t* n) {
	
    delete_req_t* new_del_req = ssmem_alloc(alloc_wf, sizeof(*new_del_req));

	new_del_req->phase = phase;
	new_del_req->tid = tid;
	new_del_req->pending = true;
	new_del_req->node = n;

	return new_del_req;	
}

uint64_t stack_size(wf_stack_t* s) {
	
	uint64_t n_elem = 0;
	node_t* tmp = s->top;
	
	while(tmp->push_tid != -1) {
		
		if(!tmp->mark) {
			n_elem++;
		}

		tmp = tmp->prev;	
	}
	
	return n_elem;
}

void push(wf_stack_t* s, int64_t tid, void* value) {

	uint64_t new_phase = FAI_U64(&s->phase_counter_push_req);

	if(s->announces[tid] != NULL) {

		ssmem_free(alloc_wf, (void*) s->announces[tid]);
	}
	
	// we post a request for our node
	s->announces[tid] = init_push_op(new_phase, init_node(value, tid));

	// we start attaching it
	help(s, s->announces[tid], tid);
}

void help(wf_stack_t* s, push_op_t* request, int64_t tid) {

	push_op_t* min_req = NULL;
	
	// first we look for the request with the smallest phase
	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		
		push_op_t* tmp = s->announces[i];
		if(tmp != NULL && !tmp->pushed && 
			 (min_req == NULL || min_req->phase > tmp->phase)) {
				 
			min_req = tmp;
		}
	}
	
	if(min_req == NULL || min_req->phase > request->phase) {
		/* if no min_req or our request has a smaller
		   phase than the min_req we found, the 
		   help is over */
		return;
	}
	
	// we first attach the min_req we found ...
	attach_node(s, min_req, tid);

	if(min_req != request) {
		// ... and next we can attach ours
		attach_node(s, request, tid);
	}
}

void attach_node(wf_stack_t* s, push_op_t* request, int64_t tid) {

	while(!request->pushed) {

		node_t* last = s->top;
		node_t* next = last->next_done;
		
		// check if stack consistent
		if(last == s->top) {
			// check if the slot is still available
			if(next == NULL ){
				// check if the request we work on 
				// is still available
				if(!request->pushed) {

					node_t* node_req = request->node;				
					
					// synchronization -> we try to put our node
					// as the future top
					if(CAS_U64_bool(&last->next_done, NULL, node_req)) {
						
						// we succeed, now we update the new top
						update_top(s, tid);
						
						// we mark the next node of the last top 
						// as unavailable
						CAS_U64_bool(&last->next_done, node_req, (void*) 1);
		
						return;
					}
				}
			}
			
			// if next != NULL, somebody is trying to 
			// update the top -> we go help him
			update_top(s, tid);
		}		
	}
}

void update_top(wf_stack_t* s, int64_t tid) {

	node_t* last = s->top;
	node_t* next = last->next_done;
	
	// if there is somebody to put at the top
	if(next != NULL && next != ((void*) 1)) {
		
		push_op_t* request = s->announces[next->push_tid];
		
		// check stack consistent
		if(last == s->top && request->node == next) {

			CAS_U64_bool(&next->prev, NULL, last); 
		
			next->index = last->index + 1;
			request->pushed = true;
			bool stat = CAS_U64_bool(&s->top, last, next);

			// if we are the one that put the node at the top
			// and the node is a new "segment"
			if(next->index % W == 0 && stat) {
				
				try_clean_up(s, next, tid, true);
			}
		}
	}
}

node_t* pop(wf_stack_t* s, int64_t tid) {

	node_t* top = s->top;
	node_t* cur = top;

	while(cur->push_tid != -1) {

		bool mark = CAS_U64_bool(&cur->mark, false, true);

		if(mark) {
			break;
		}

		cur = cur->prev;
	}

	if(cur->push_tid == -1) {

		return EMPTY_STACK;
	}

	void* result = cur->value;
	
	// we marked a node, we try to clean up a "segment"
	try_clean_up(s, cur, tid, false);

	return result;
}

void try_clean_up(wf_stack_t* s, node_t* n, int64_t tid, bool from_right_node) {

	node_t* tmp = NULL;
	
	/* two cases when you clean up a "segment":
		1. you pushed a node a the head of a new "segment"	
		2. you popped one then you take the node you popped
	*/
	if(from_right_node) {
		tmp = n->prev;
	} else {
		tmp = n;
	}

	while(tmp->push_tid != -1) {

		if(tmp->index % W == 0) {
			
			// we increase the counter at the tail of the "segment"
			if(IAF_U64(&tmp->counter) == W + 1) {
				/* if every node in a "segment" is popped
				AND there is a node after us, we can free the
				node of the "segment" */
				clean(s, tid, tmp); 
			} 
			break;
		}

		tmp = tmp->prev;
	}
}

void clean(wf_stack_t* s, int64_t tid, node_t* n) {

	if(s->all_delete_requests[tid] != NULL) {
		
		ssmem_free(alloc_wf, (void*) s->all_delete_requests[tid]);
	}

	uint64_t phase = FAI_U64(&s->phase_counter_del_req);
	
	// we post a request to delete a "segment"
	s->all_delete_requests[tid] = init_delete_req(phase, tid, n);
	
	// we go delete it 
	help_delete(s, s->all_delete_requests[tid], tid);
}

void help_delete(wf_stack_t* s, delete_req_t* dr, int64_t tid) {

	delete_req_t* min_req = NULL;
	
	// first we look for the request with the smallest phase
	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		
		delete_req_t* tmp = s->all_delete_requests[i];
		if(tmp != NULL && tmp->pending && 
			(min_req == NULL || min_req->phase > tmp->phase)) {
				
			min_req = tmp;
		}
	}
	
	if(min_req == NULL || min_req->phase > dr->phase) {

		return;
	}
	
	// we delete the smallest request
	unique_delete(s, min_req, tid);

	if(min_req != dr) {
		
		// we delete our request
		unique_delete(s, dr, tid);
	}
}

void unique_delete(wf_stack_t* s, delete_req_t* dr, int64_t tid) {

	while(dr->pending) {
		
		// we get the current unique_req
		delete_req_t* cur = s->unique_req;
		
		// we wait until the current unique_req is pending
		if(cur == NULL || !cur->pending) {
			
			if(dr->pending) {
				
				// we put our delete_req as the unique_req
				bool stat = dr != cur ? CAS_U64_bool(&s->unique_req, cur, dr) : true;
				
				// we process our request
				help_finish_delete(s, tid);
				
				if(stat) {

					return;
				} 
			}

		} else {
			
			// we process the current unique_req
			help_finish_delete(s, tid);
		}
	}
}

void help_finish_delete(wf_stack_t* s, int64_t tid) {

	delete_req_t* cur = s->unique_req;
	
	if(!cur->pending) {

		return;
	}
	
	uint64_t end_idx = cur->node->index + W - 1;
	node_t* right_node = s->top;
	node_t* left_node = right_node->prev;
	
	// get the node at the right of the "segment"
	while(left_node->index != end_idx && left_node->push_tid != -1) {

		right_node = left_node;
		left_node = left_node->prev;
	}
	
	if(left_node->push_tid == -1) {
		
		return;
	}
	
	// get the node at the left of the "segment"
	node_t* target = left_node;
	uint64_t i;
	for(i = 0; i < W; i++) {

		target = target->prev;
	}
	
	// we remove the "segment" from the stack
	if(CAS_U64_bool(&right_node->prev, left_node, target)) {
		
		// we deallocate the node of the "segment"
		node_t* tmp;
		for(i = 0; i < W; i++) {
			
			tmp = left_node;
			left_node = left_node->prev;
			ssmem_free(alloc_wf, (void*) tmp);
		}
	}
	
	cur->pending = false;
}

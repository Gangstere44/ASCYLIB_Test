

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic_ops_if.h"
#include "wait_free_stack.h"

wf_stack_t* init_wf_stack(uint64_t num_thr) {
	
	wf_stack_t* new_stack = malloc(sizeof(wf_stack_t));
	
	new_stack->num_thr = num_thr;
	new_stack->sentinel.push_tid = -1; 
	new_stack->top = &new_stack->sentinel;
	
	new_stack->announces = calloc(num_thr, sizeof(push_op_t*));
	new_stack->all_delete_requests = calloc(num_thr, sizeof(delete_req_t*));
	uint64_t i;
	for(i = 0; i < num_thr; i++) {
		new_stack->annouces[i] = init_push_op();
		new_stack->all_delete_requests[i] = init_del_req();
	}
	
	new_stack->phase_counter_push_req = 0;
	new_stack->phase_counter_del_req = 0;
	new_stack->unique_req = NULL;
	
	return new_stack;
}

node_t* init_node(void* value, int64_t push_tid) {

	node_t* new_node = malloc(sizeof(node_t));
	
	new_node->value = value;
	new_node->next_done = NULL;
	new_node->prev = NULL;
	new_node->mark = false;
	new_node->push_tid = push_tid;
	new_node->index = 0;
	new_node->index = 0;
	
	return new_node;
}

push_op_t* init_push_op(void) {
	
	push_op_t* new_push_op = malloc(sizeof(push_op_t));
	new_push_op->phase = 0;
	new_push_op->pushed = true;
	new_push_op->new_node = NULL;
	
	return new_push_op;	
}

delete_req_t* init_delete_req(void) {
	
	delete_req_t* new_del_req = malloc(sizeof(delete_req_t));
	new_del_req->phase = 0;
	new_del_req->tid = -1;
	new_del_req->pending = false;
	new_del_req->node = NULL;
	
}


void push(wf_stack_t* s, uint64_t tid, void* value) {
	
	uint64_t new_phase = FAI_U64(&s->phase_counter_push_req);

	s->announces[tid]->phase = new_phase;
	s->announces[tid]->node = init_node(value, tid);
	s->announces[tid]->pushed = false;

	help(s, s->annouces[tid]);
	
}
void help(wf_stack_t* s,push_op_t* request) {
	
	int64_t min_tid = 0;
	push_op_t* min_req = NULL;
	
	bool first = true;
	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		
		push_op_t* tmp = s->announces[i];
		if(!tmp->pushed && 
			(first || (min_req != NULL && min_req->phase > tmp->phase))) {
			min_tid = i;
			min_req = tmp;
		}
	}
	
	if(min_req == NULL || min_req->phase > request->phase) {
		return;
	}
	
	attach_node(s, min_req);
	
	if(min_req != request) {
		attach_node(s, request);
	}
}

void attach_node(wf_stack_t* s,push_op_t* request) {

	while(!request->pushed) {
		
		node_t* last = s->top;
		
		node_t* next = next_done;
		bool done = last->mark;
		
		if(last == s->top) {
			if(next == NULL && !done) {
				if(!request.pushed) {
					node_t* node_req = request->node;
					if(CAS_U64_bool(&last->next_done, NULL, node_req)) {
						update_top(s);
						CAS_U64_bool(&last->next_done->mark, false, true);
						CAS_U64_bool(&last->next_done, node_req, NULL);
						return;
					}
				}
			}
			update_top(s);
		}
	}
}

void update_top(wf_stack_t* s) {
	
	node_t* last = s->top;
	node_t* next = next_done;

	if(next != NULL) {
		
		push_op_t* request = s->announces[next->push_tid];
		
		if(last == s->top && request->node == next) {
			
			CAS_U64_bool(&next->prev, NULL, last); 
			next->index = last->index + 1;
			request->pushed = true;
			bool stat = CAS_U64_bool(&s->top, last, next);
			if(next->index % W == 0 && stat) {
				tryClean(s, next);
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
	
	try_clean_up(s, cur, tid);
	
	return cur;
}

void try_clean_up(wf_stack_t* s, node_t* n, int64_t tid) {

	node_t* tmp = n->prev;
	
	while(tmp->push_tid != -1) {
		if(tmp->index % W == 0) {
			if(FAI_U64(&tmp->counter) == W + 1) {
				clean(s, tid, tmp); 
			}
			break;
		}
		
		temp = temp->prev;
	}
	
}

void help_finish_delete(wf_stack_t* s) {
	
	delete_req_t* cur = s->unique_req;
	
	if(!cur_req->pending) {
		return;
	}
	
	uint64_t end_idx = cur->node->index + W - 1;
	node_t* right_node = s->top;
	node_t* left_node = right_node->prev;
	
	while(left_node.index != end_idx && left_node.push_tid != -1) {
		right_node = left_node;
		left_node = left_node->prev;
	}
	
	if(left_node.push_tid == -1) {
		return;
	}
	
	node_t* target = left_node;
	uint64_t i;
	for(i = 0; i < W; i++) {
		target = target.prev;
	}
	
	CAS_U64_bool(&right_node->prev, left_node, target);
	
	node_t* tmp;
	for(i = 0; i < W; i++) {
		tmp = left_node;
		left_node = left_node.prev;
		free(tmp);
	}
	
	cur->pending = false;
}

void clean(wf_stack_t* s, uint64_t tid, node_t* n) {

	uint64_t phase = FAI_U64(&s->phase_counter_del_req);
	
	s->all_delete_requests[tid]->pahse = phase;
	s->all_delete_requests[tid]->tid = tid;
	s->all_delete_requests[tid]->node = n;
	s->all_delete_requests[tid]->pending = true;
	
	help_delete(s, s->all_delete_requests[tid]);
}

void help_delete(wf_stack_t* s, delete_req_t* dr) {
	
	int64_t min_tid = 0;
	delete_req_t* min_req = NULL;
	
	bool first = true;
	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		
		delete_req_t* tmp = s->all_delete_requests[i];
		if(!tmp->pushed && 
			(first || (min_req != NULL && min_req->phase > tmp->phase))) {
			min_phase = tmp->phase;
			min_tid = i;
		}
	}
	
	if(min_req == NULL || min_req->phase > dr->phase) {
		return;
	}
	
	unique_delete(s, min_req);
	
	if(min_req != request) {
		unique_delete(s, dr);
	}
}

void unique_delete(wf_stack_t* s, delete_req_t* dr) {
	
	while(dr.pending) {
		delete_req_t* cur = s->unique_req;
		if(!cur.pending) {
			if(dr.pending) {
				bool stat = dr != cur ? CAS_U64_bool(&s->unique_req, cur, dr) : true;
				help_finish_delete(s);
				if(stat) {
					return;
				}
			}
		} else {
			help_finish_delete(s);
		}
	}
	
}
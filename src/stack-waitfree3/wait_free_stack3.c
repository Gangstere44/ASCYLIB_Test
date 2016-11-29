

#include "wait_free_stack3.h"

__thread ssmem_allocator_t* alloc_wf;

wf_stack_t* init_wf_stack(uint64_t num_thr) {

	wf_stack_t* new_stack = malloc(sizeof(wf_stack_t));
	
	new_stack->num_thr = num_thr;
	
	new_stack->top_id = 0;
	new_stack->top = wf_new_segment(0, NULL);
	
	new_stack->handles = calloc(num_thr, sizeof(handle_t));
	uint64_t i;
	for(i = 0; i < num_thr; i++) {
		new_stack->handles[i].ttd = i == num_thr - 1 ? 0 : i + 1;
	}
	
	new_stack->clean_tid = -1;

	return new_stack;
}

wf_segment_t* wf_new_segment(uint64_t id, wf_segment_t* prev) {
	
	wf_segment_t* new_seg = ssmem_alloc(alloc_wf, sizeof(wf_segment_t));
	new_seg->prev = prev;
	new_seg->id = id;
	uint64_t i;
	for(i = 0; i < W; i++) {
		new_seg->cells[i] = NULL;
	}
	
	return new_seg;
}

cell_t* wf_find_cell(wf_stack_t* s, uint64_t cell_id) {

	uint64_t i;
	
	cell_t* result = NULL;
	wf_segment_t* top;
	while(result == NULL) {
		
		top = s->top;
		
		if(top->id * W > cell_id) {
			
			top = top->prev;
			
		} else if(s->id* W + W - 1 < cell_id) {
			
			wf_segment_t* tmp = wf_new_segment(s->id + 1, top);
			if(!CAS_U64_bool(&s->top_segment, top, tmp)) {
				ssmem_free(alloc_wf, (void*) tmp);
			}
		} else {

			result = &top->cells[cell_id % W];		
		}
	}
	
	return result;
}

uint64_t stack_size(wf_stack_t* s) {
	
	uint64_t n_elem = 0;
	wf_segment_t* tmp = s->top;
	
	while(tmp != NULL) {
		
		uint64_t i;
		for(i = 0; i < W; i++) {
			
			if(tmp->cells[i] != NULL && tmp->cells[i] != MARKED) {
				n_elem++;
			}
		}

		tmp = tmp->prev;	
	}
	
	return n_elem;
}

void push(wf_stack_t* s, int64_t tid, void* value) {
	
	uint64_t cell_id = FAI(&s->top_id);
	
	cell_t* c = find_cell(s, cell_id);
	
	c->value = value;
}

void* pop(wf_stack_t* s, int64_t tid) {
	
	wf_segment_t* tmp = s->top;
	
	while(tmp != NULL) {
		
		uint64_t i;
		for(i = 0; i < W; i++) {
			
			void* val = s->cells[i];
			
			if(val != EMPTY && val != MARKED) {
				
				if(CAS_U64_bool(&s->cells[i], val, MARKED)) {
					
					try_clean_up(s, tid);
					
					return val;
				}
			}
		}
		
	}
	
	try_clean_up(s, tid);
	
	return EMPTY;
}

void try_clean_up(wf_stack_t* s, int64_t tid) {
	
	if(s->clean_tid != -1) {
		return;
	}
	
	if(CAS_U64_bool(&s->clean_tid, -1, tid)) {
		
		wf_segment_t* left = s->top;
		wf_segment_t* right = left->prev;
		
		while(right != NULL) {
			
			uint64_t i;
			bool to_free = true;
			for(i = 0; i < W && to_free; i++) {
				if(right->cells[i] != MARKED) {
					to_free = false;
				}
			}
			
			if(to_free) {
				
				left->prev = right->prev;
				ssmem_free(alloc_wf, (void*) right);
				right = left->prev;	
				
			} else {
				left = right;
				right = right->prev;
			}
			
		}
		
		s->clean_tid = -1;
	}
}


/*
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
*/

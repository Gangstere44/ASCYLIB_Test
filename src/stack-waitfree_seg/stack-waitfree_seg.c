
#include "stack-waitfree_seg.h"

__thread ssmem_allocator_t* alloc_wf;

wf_stack_t* init_wf_stack(uint64_t num_thr) {

	wf_stack_t* new_stack = malloc(sizeof(wf_stack_t));

	new_stack->num_thr = num_thr;

	new_stack->sentinel.id = -1;

	new_stack->top_id = 0;
	new_stack->top = &new_stack->sentinel;
	
	new_stack->clean_tid = -1;

	return new_stack;
}

wf_segment_t* wf_new_segment(int64_t id, wf_segment_t* prev) {

	wf_segment_t* new_seg = ssmem_alloc(alloc_wf, sizeof(wf_segment_t));

	new_seg->prev = prev;
	new_seg->id = id;
	uint64_t i;
	for(i = 0; i < W; i++) {
		new_seg->cells[i] = EMPTY;
	}

	return new_seg;
}

uint64_t stack_size(wf_stack_t* s) {

	uint64_t n_elem = 0;
	wf_segment_t* tmp = s->top;

	while(tmp->id != -1) {

		uint64_t i;
		for(i = 0; i < W; i++) {

			if(tmp->cells[i] != EMPTY && tmp->cells[i] != MARKED) {
				n_elem++;
			}
		}

		tmp = tmp->prev;
	}

	return n_elem;
}

void add_segment(wf_stack_t* s, int64_t seg_id, wf_segment_t* prev) {

	wf_segment_t* tmp_next = wf_new_segment(seg_id, prev);
	if(!CAS_U64_bool(&s->top, prev, tmp_next)) {
		/* if we fail to add our segment, somebody else
		succeeded -> we reclaim our */
		ssmem_free(alloc_wf, (void*) tmp_next);
	}
}

cell_t* find_cell(wf_stack_t* s, uint64_t cell_id) {

	wf_segment_t* tmp = s->top;

	/* we add segment until the top of the stack as a
	sufficiently high id to accept our cell_id */
	while(tmp->id == -1 || tmp->id * W + W - 1 < cell_id) {

		add_segment(s, tmp->id + 1, tmp);
		tmp = s->top;
	}

	/* we go through the segment until we find the 
	right one */
	while(tmp->id * W > cell_id) {

		tmp = tmp->prev;
	}

	return &tmp->cells[cell_id % W];
}


void push(wf_stack_t* s, int64_t tid, void* value) {
		
	uint64_t cell_id = FAI_U64(&s->top_id);

	cell_t* c = find_cell(s, cell_id);
	
	c->value = value;
}

void* pop(wf_stack_t* s, int64_t tid) {

    wf_segment_t* tmp = s->top;

    while(tmp->id != -1) {

		int64_t i;
		for(i = W - 1; i >= 0; i--) {

			void* val = tmp->cells[i];

			if(val != EMPTY && val != MARKED) {

				if(CAS_U64_bool(&tmp->cells[i], val, MARKED)) {

                    try_clean_up(s, tid);
					
					return val;
				}
			}
		}

		tmp = tmp->prev;
    }

    try_clean_up(s, tid);

    return EMPTY;
}

void try_clean_up(wf_stack_t* s, int64_t tid) {

	/* we can't clean if somebody else is already cleaning
	OR the top is the sentinel OR there is only one other
	segment after the sentinel */
	if(s->clean_tid != -1 || s->top->id == -1 
		|| s->top->prev->id == -1) {
		return;
	}

	if(CAS_U64_bool(&s->clean_tid, -1, tid)) {

		wf_segment_t* left = s->top;
		wf_segment_t* right = left->prev;

		bool to_free = true;
		while(left->id != -1 && right->id != -1 && to_free) {

			uint64_t i;
			for(i = 0; i < W && to_free; i++) {
				if(right->cells[i] != MARKED) {
					to_free = false;
				}
			}

			if(to_free) {

				left->prev = right->prev;
				ssmem_free(alloc_wf, (void*) right);
				right = left->prev;

			} 
		}

		s->clean_tid = -1;
	}
}



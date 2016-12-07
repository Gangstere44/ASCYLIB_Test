
#include "wait_free_stack4.h"

__thread ssmem_allocator_t* alloc_wf;

wf_stack_t* init_wf_stack(uint64_t num_thr) {

	wf_stack_t* new_stack = malloc(sizeof(wf_stack_t));

	new_stack->num_thr = num_thr;

	new_stack->top_id = 0;
	new_stack->real_top_id = 0;
	new_stack->top = NULL;

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
		new_seg->cells[i] = EMPTY;
	}

	return new_seg;
}

cell_t* wf_find_cell(wf_stack_t* s, uint64_t cell_id) {

	if(s->top == NULL) {

		wf_segment_t* tmp = wf_new_segment(0, NULL);
		if(!CAS_U64_bool(&s->top, NULL, tmp)) {
			ssmem_free(alloc_wf, (void*) tmp);
		}

	}

	wf_segment_t* tmp = s->top;
	while(tmp == NULL) {
		tmp = s->top;
	}

	while(tmp->id * W + W - 1 < cell_id) {
		wf_segment_t* tmp_next = wf_new_segment(tmp->id + 1, tmp);
		if(!CAS_U64_bool(&s->top, tmp, tmp_next)) {
			ssmem_free(alloc_wf, (void*) tmp_next);
		}
		tmp = s->top;
	}

	while(tmp->id * W > cell_id) {
		tmp = tmp->prev;
	}
	
	return &tmp->cells[cell_id % W];
}

uint64_t stack_size(wf_stack_t* s) {

	uint64_t n_elem = 0;
	wf_segment_t* tmp = s->top;

	uint64_t remain = 0;

	while(tmp != NULL) {

		uint64_t i;
		for(i = 0; i < W; i++) {

			if(tmp->cells[i] != EMPTY && tmp->cells[i] != MARKED) {
				n_elem++;
				remain += (uint64_t) tmp->cells[i];
			}
		}

		tmp = tmp->prev;
	}

	printf("remain in the stack : %lu \n", remain);

	return n_elem;
}

void push(wf_stack_t* s, int64_t tid, void* value) {

	bool success = false;
	while(!success) {
		uint64_t cell_id = FAI_U64(&s->top_id);

		cell_t* c = wf_find_cell(s, cell_id);

		c->value = value;
		
		if(s->num_thr > 1) {
			while(s->real_top_id != cell_id);
			
			s->real_top_id++;
		
			success = true;
		} else {
			success = true;
		}	
	}
}

void* pop(wf_stack_t* s, int64_t tid) {

	if(s->num_thr == 1) {

		uint64_t cell_id = s->top_id - 1;
		if(cell_id == -1) {
			return EMPTY;
		}

		s->top_id--;

		cell_t* c = wf_find_cell(s, cell_id);
		void* result = c->value;

		c->value = MARKED;

		if(cell_id % W == 0) {
			try_clean_up(s, tid);
		}

		return result;

	} else {
		
		uint64_t top_index = s->real_top_id;
	    wf_segment_t* tmp = s->top;
		
		while(tmp != NULL && tmp->id * W > top_index) {
			tmp = tmp->prev;
		}
		
	    while(tmp != NULL) {

	    	int64_t i;
			for(i = W - 1; i >= 0; i--) {
				if(top_index >= W * tmp->id + i) {
					void* val = tmp->cells[i];

					if(val != EMPTY && val != MARKED) {

						if(CAS_U64_bool(&tmp->cells[i], val, MARKED)) {

							try_clean_up(s, tid);

							return val;
						}
					}
				}
			}

			tmp = tmp->prev;
	    }

	    try_clean_up(s, tid);

	    return EMPTY;
	}
}

void try_clean_up(wf_stack_t* s, int64_t tid) {

	if(s->num_thr == 1) {

		wf_segment_t* last = s->top;
		if(last->id != 0) {
			s->top = last->prev;
			ssmem_free(alloc_wf, (void*) last);
		}

		return;
	}

	if(s->clean_tid != -1 || s->top == NULL) {
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


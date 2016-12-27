
/* strat 1 , only one seg at a time */
// 1 => 19.008
// 2 => 15.085
// 4 => 7.44
// 8 => 7.408
// 16 => 5.560

/* start 2, 2 at a time */
// 1 => 18.094
// 2 => 15.060
// 4 => 8.281
// 8 => 5.487
// 16 => 4.835

/* start 2, 4 at a time */
// 1 => 15.568
// 2 => 14.666
// 4 => 9.004
// 8 => 5.510	
// 16 => 4.918


#include "wait_free_stack3.h"

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

	new_stack->sentinel.id = -1;

	new_stack->top_id = 0;
	new_stack->top = &new_stack->sentinel;

	new_stack->handles = calloc(num_thr, sizeof(handle_t));
	uint64_t i;
	for(i = 0; i < num_thr; i++) {

		new_stack->handles[i].pop1_lat = 0;
		new_stack->handles[i].pop1_count = 0;
		new_stack->handles[i].pop2_lat = 0;
		new_stack->handles[i].pop2_count = 0;
		new_stack->handles[i].push_lat = 0;
		new_stack->handles[i].push_count = 0;
	}

	
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

	uint64_t remain = 0;

	while(tmp->id != -1) {

		uint64_t i;
		for(i = 0; i < W; i++) {

			if(tmp->cells[i] != EMPTY && tmp->cells[i] != MARKED) {
				n_elem++;
				remain += (uint64_t) tmp->cells[i];
			}
		}

		tmp = tmp->prev;
	}

	return n_elem;
}

void add_segment(wf_stack_t* s, int64_t seg_id, wf_segment_t* prev) {

	wf_segment_t* tmp_next = wf_new_segment(seg_id, prev);
	if(!CAS_U64_bool(&s->top, prev, tmp_next)) {
		ssmem_free(alloc_wf, (void*) tmp_next);
	}
}

cell_t* wf_find_cell(wf_stack_t* s, uint64_t cell_id) {

	wf_segment_t* tmp = s->top;

	while(tmp->id == -1 || tmp->id * W + W - 1 < cell_id) {

		add_segment(s, tmp->id + 1, tmp);
		tmp = s->top;
	}

	while(tmp->id * W > cell_id) {
		tmp = tmp->prev;
	}

	return &tmp->cells[cell_id % W];
}


void push(wf_stack_t* s, int64_t tid, void* value) {
	
//	START_TS();
	
	uint64_t cell_id = FAI_U64(&s->top_id);

	cell_t* c = wf_find_cell(s, cell_id);
	
	c->value = value;
/*	
	END_TS();
	ADD_DUR(s->handles[tid].push_lat);
	s->handles[tid].push_count++;
*/
}

void* pop(wf_stack_t* s, int64_t tid) {

//	START_TS();

    wf_segment_t* tmp = s->top;

    while(tmp->id != -1) {

		int64_t i;
		for(i = W - 1; i >= 0; i--) {

			void* val = tmp->cells[i];

			if(val != EMPTY && val != MARKED) {

				if(CAS_U64_bool(&tmp->cells[i], val, MARKED)) {

                    try_clean_up(s, tid);

  /*          		END_TS();
					ADD_DUR(s->handles[tid].pop1_lat);
					s->handles[tid].pop1_count++;
*/
					return val;
				}
			}
		}

		tmp = tmp->prev;
    }

    try_clean_up(s, tid);

/*
	END_TS();
	ADD_DUR(s->handles[tid].pop1_lat);
	s->handles[tid].pop1_count++;		
*/
    return EMPTY;
}

void try_clean_up(wf_stack_t* s, int64_t tid) {

//	START_TS();

	if(s->clean_tid != -1 || s->top->id == -1 || s->top->prev->id == -1) {
/*
			END_TS();
			ADD_DUR(s->handles[tid].pop2_lat);
			s->handles[tid].pop2_count++;
*/
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
/*
	END_TS();
	ADD_DUR(s->handles[tid].pop2_lat);
	s->handles[tid].pop2_count++;
*/
}

void print_profiling(wf_stack_t* s) {
/*
	handle_t h = {0, 0, 0, 0, 0, 0};

	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		h.pop1_lat += s->handles[i].pop1_lat;
		h.pop1_count += s->handles[i].pop1_count;

		h.pop2_lat += s->handles[i].pop2_lat;
		h.pop2_count += s->handles[i].pop2_count;

		h.push_lat += s->handles[i].push_lat;
		h.push_count += s->handles[i].push_count;
	}

	h.pop1_lat -= h.pop2_lat;
	h.pop1_lat /= h.pop1_count;

	h.pop2_lat /= h.pop2_count;

	h.push_lat /= h.push_count;
*/
//	printf("/***** LATENCY *****/ \n");
//	printf("Pop1 : \t %lu  \nPop2 : \t %lu \nPush: \t %lu\n", h.pop1_lat, h.pop2_lat, h.push_lat);
//	printf("/*******************/\n");

}


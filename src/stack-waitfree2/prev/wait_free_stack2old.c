//
// Created by Quentin on 02.10.2016.
//



#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic_ops_if.h"
#include "wait_free_queue.h"

#define PATIENCE 10
#define MAX_GARBAGE(n) (10 * n)

// helper functions

wf_stack_t* init_wf_stack(uint64_t num_thr) {
	
	wf_stack_t* stack = malloc(sizeof(wf_stack_t));
	stack->num_thr = num_thr;
	stack->top = 0;
	
	queue->sentinel.id = -1;
	queue->sentinel.prev = NULL;
	queue->sentinel.is_sentinel = true;
	
	queue->top_segment = wf_new_segment(0);
	queue->top_segment->prev = &queue->sentinel;
	
	return stack;
}

void init_wf_handle(wf_handle_t* handle, wf_segment_t* init_seg, uint64_t tid) {
	
		handle->head = init_seg;
		handle->tail = init_seg;
		handle->next = NULL;
		handle->hzdp = NULL;
		handle->tid = tid;
		handle->enq.peer = handle;
		handle->enq.req.val = TAIL_CONST_VAL;
		handle->enq.req.state.pending = 0;
		handle->enq.req.state.id = 0;
		handle->enq.id = 0;
		handle->deq.peer = handle;
		handle->deq.req.id = 0;
		handle->deq.req.state.pending = 0;
		handle->deq.req.state.idx = 0;
		handle->deq.id = 0;

		#ifdef CHECK_CORRECTNESS
		handle->sum_enq = 0;
		handle->sum_deq = 0;
		#endif

		#ifdef RECORD_F_S
		handle->fast_enq = 0;
		handle->slow_enq = 0;
		handle->fast_deq = 0;
		handle->slow_deq = 0;
		#endif
}

wf_segment_t * wf_new_segment(uint64_t id) {

	wf_segment_t* new_seg = malloc(sizeof(wf_segment_t));
	new_seg->id = id;
	new_seg->prev = NULL;

	uint64_t i;
	for(i = 0; i < SEG_LENGTH; i++) {
        cell_t* cell = &(new_seg->cells[i]);
        cell->val =  EMPTY;
        cell->enq =  TAIL_ENQ_VAL;
        cell->deq =  TAIL_DEQ_VAL;
		cell->mark = false;
	}

	return new_seg;
}

cell_t* wf_find_cell(wf_stack_t* s, uint64_t cell_id) {

	uint64_t i;
	
	cell_t* result = NULL;
	while(result == NULL) {
		
		wf_segment_t* top = s->top_segment;
		
		if(s->is_sentinel) {
			
			wf_segment_t* tmp = wf_new_segment(s->id + 1);
			tmp->prev = s;
			if(!CAS_U64_bool(&s->top_segment, top, tmp)) {
				free(tmp);
			}
			
		} else if(s->id * SEG_LENGTH > cell_id) {
			
			s = s->prev;
			
		} else if(s->id* SEG_LENGTH + SEG_LENGTH - 1 < cell_id) {
			
			wf_segment_t* tmp = wf_new_segment(s->id + 1);
			tmp->prev = s;
			if(!CAS_U64_bool(&s->top_segment, top, tmp)) {
				free(tmp);
			}
		} else {

			result = &top->cells[cell_id % SEG_LENGTH];
			
		}
		
	}
	
	return result;
}
	

void wf_advance_end_for_linearizability(uint64_t* E, uint64_t cid) {

	uint64_t e;
	do {
		e = *E;
	} while(e < cid && !CAS_U64_bool(E, e, cid));
}

uint64_t wf_stack_size(wf_stack_t* s) {
	
	wf_segment_t* seg = s->top;
	
	uint64_t size = 0;
	while(!seg.is_sentinel) {
		
		uint64_t i;
		for(i = 0; i < SEG_LENGTH; i++) {
			if(!seg->mark && seg->value != NULL) {
				size++;
			}
		}
		
		seg = seg->prev;
	}
	
	return size;
}

void wf_reclaim_records(wf_stack_t* s, wf_handle_t* h) {
/*
	#ifdef RECORD_F_S
	while(!CAS_U64_bool(&q->tot_fast_enq, q->tot_fast_enq, (q->tot_fast_enq + h->fast_enq)));

	while(!CAS_U64_bool(&q->tot_slow_enq, q->tot_slow_enq, (q->tot_slow_enq + h->slow_enq)));

	while(!CAS_U64_bool(&q->tot_fast_deq, q->tot_fast_deq, (q->tot_fast_deq + h->fast_deq)));

	while(!CAS_U64_bool(&q->tot_slow_deq, q->tot_slow_deq, (q->tot_slow_deq + h->slow_deq)));
	#endif
	*/
}

void wf_reclaim_correctness(wf_stack_t* s, wf_handle_t* h) {
/*
	#ifdef CHECK_CORRECTNESS
	while(!CAS_U64_bool(&q->tot_sum_enq, q->tot_sum_enq, (q->tot_sum_enq + h->sum_enq)));

	while(!CAS_U64_bool(&q->tot_sum_deq, q->tot_sum_deq, (q->tot_sum_deq + h->sum_deq)));
	#endif
*/
}

void wf_sum_queue(wf_stack_t* s) {
/*
	#ifdef CHECK_CORRECTNESS
	uint64_t i;
	uint64_t tmp_sum = 0;
	wf_segment_t* s = q->q;
	for(i = q->headQ; i < q->tailQ; i++) {
		cell_t* c = wf_find_cell(&s, i);
		tmp_sum += c->val;
	}

	while(!CAS_U64_bool(&q->tot_sum_deq, q->tot_sum_deq, (q->tot_sum_deq + tmp_sum)));
	#endif
*/
}

// Enqueue functions

void wf_enqueue(wf_stack_t* s, wf_handle_t* h, void* v) {

	#ifdef CHECK_CORRECTNESS
	h->sum_enq += v;
	#endif

	h->hzdp = h->tail;
	uint64_t cell_id = 0;
	uint64_t p;
	for(p = 0; p < PATIENCE; p++) {
		if(wf_enq_fast(s, h, v, &cell_id)) {

			#ifdef RECORD_F_S
			h->fast_enq++;
			#endif

			return;
		}
	}
	wf_enq_slow(s, h, v, cell_id);


	#ifdef RECORD_F_S
	h->slow_enq++;
	#endif

	h->hzdp = NULL;
}

bool wf_try_to_claim_req(uint64_t* s, uint64_t id, uint64_t cell_id) {
	return CAS_U64_bool(s, (0x8000000000000000 | id), (0x7FFFFFFFFFFFFFFF & cell_id));
}

void wf_enq_commit(wf_stack_t* s, cell_t* c, void* v, uint64_t cid) {

	wf_advance_end_for_linearizability(&(q->tailQ), cid + 1);
	c->val = v;
}

bool wf_enq_fast(wf_stack_t* s, wf_handle_t* h, void* v, uint64_t* cid) {
	uint64_t i = FAI_U64(&(q->tailQ));
	cell_t* c = wf_find_cell(&(h->tail), i);
	if(CAS_U64_bool(&(c->val), EMPTY, v)) {
		return true;
	}

	*cid = i;
	return false;
}

void wf_enq_slow(wf_stack_t* s, wf_handle_t* h, void* v, uint64_t cell_id) {

	// we add a request to ourselves, like this one of our peer is able to help us
	wf_enq_request_t* r = &(h->enq.req);
	r->val = v;
	r->state.id = cell_id;
	r->state.pending = 1;
	
	// while waiting, we still try to enqueue the value
	wf_segment_t* tmp_tail = h->tail;
	do {
		
		// we take the next possible cell
		uint64_t i = FAI_U64(&(q->tailQ));
		cell_t* c = wf_find_cell(&tmp_tail, i);
		// and we try to claim it
		if(CAS_U64_bool(&(c->enq), TAIL_ENQ_VAL, r) && c->val == TAIL_CONST_VAL) {
			wf_try_to_claim_req((uint64_t*) &(r->state), cell_id, i);
			break;
		}
	} while(r->state.pending);
	
	uint64_t id = r->state.id;
	cell_t* c = wf_find_cell(&(h->tail), id);
	
	wf_enq_commit(q, c, v, id);
}

void* wf_help_enq(wf_stack_t* s, wf_handle_t* h, cell_t* c, uint64_t i) {
	if(!CAS_U64_bool(&(c->val), TAIL_CONST_VAL, HEAD_CONST_VAL) && c->val != (void*) HEAD_CONST_VAL) {
		return c->val;
	}
	
	wf_handle_t* volatile p;
	wf_enq_request_t* r;
	if(c->enq == TAIL_ENQ_VAL) {
			
		do {
			
			p = h->enq.peer;
			r = &(p->enq.req);

			if(h->enq.id == 0 || h->enq.id == r->state.id) {
				break;
			}
			
			h->enq.id = 0;
			h->enq.peer = p->next;
			
		} while(1);
		
		if(r->state.pending && r->state.id <= i && !CAS_U64_bool(&(c->enq), TAIL_ENQ_VAL, r)) {
			h->enq.id = r->state.id;
		} else {
			h->enq.peer = p->next;
		}
		
		if(c->enq == TAIL_ENQ_VAL) {
			CAS_U64_bool(&(c->enq), TAIL_ENQ_VAL, HEAD_ENQ_VAL);
		}
	}

	if(c->enq == HEAD_ENQ_VAL) {
		return (q->tailQ <= i ? EMPTY : HEAD_CONST_VAL);
	}
	
	r = c->enq;
	
	if(r->state.id > i) {
		
		if(c->val == HEAD_CONST_VAL && q->tailQ <= i) {
			return EMPTY;
		}
	} else if (wf_try_to_claim_req((uint64_t*) &(r->state), r->state.id, i) ||
					((r->state.pending == 0 && r->state.id == i) && c->val == HEAD_CONST_VAL)) {
		wf_enq_commit(q, c, r->val, i);
	}
	
	return c->val;
}

// dequeue functions

void* wf_dequeue(wf_stack_t* s, wf_handle_t* h) {
	h->hzdp = h->head;
	void* v = NULL;
	uint64_t cell_id = 0;
	
	int p = 0;
	for(p = PATIENCE; p >= 0; p--) {
		v = wf_deq_fast(q, h, &cell_id);
		
		if(v != HEAD_CONST_VAL)  {
			break;
		}
	}
	
	if(v == HEAD_CONST_VAL) {
		v = wf_deq_slow(q, h, cell_id);

		#ifdef RECORD_F_S
		h->slow_deq++;
		#endif
	} else {
		#ifdef RECORD_F_S
		h->fast_deq++;
		#endif
	}
	
	if(v != EMPTY) {
		wf_help_deq(q, h, (wf_handle_t*) h->deq.peer);
		h->deq.peer = ((wf_handle_t*) h->deq.peer)->next;
	}


	#ifdef CHECK_CORRECTNESS
	h->sum_deq += v;
	#endif

	h->hzdp = NULL;
	//wf_cleanup(q, h);
	
	return v;
}

void* wf_deq_fast(wf_stack_t* s, wf_handle_t* h, uint64_t* id) {
	uint64_t i = FAI_U64(&(q->headQ));
	cell_t* c = wf_find_cell(&(h->head), i);
	void* v = wf_help_enq(q, h, c, i);
	
	if(v == EMPTY) {
		return EMPTY;
	}
	
	if(v != HEAD_CONST_VAL && CAS_U64_bool(&(c->deq), TAIL_DEQ_VAL, HEAD_DEQ_VAL)) {
		return v;
	}
	
	*id = i;
	
	return HEAD_CONST_VAL;
}

void* wf_deq_slow(wf_stack_t* s, wf_handle_t* h, uint64_t cid) {
	wf_deq_request_t* r = &(h->deq.req);
	r->id = cid;
	r->state.idx = cid;
	r->state.pending = 1;
	
	wf_help_deq(q, h, h);
	
	uint64_t i = r->state.idx;
	cell_t* c = wf_find_cell(&(h->head), i);
	void* v = c->val;
	
	wf_advance_end_for_linearizability(&(q->headQ), i + 1);
	
	return (v == HEAD_CONST_VAL ? EMPTY : v);
	
}

void wf_help_deq(wf_stack_t* s, wf_handle_t* h, wf_handle_t* helpee) {
	wf_deq_request_t* r = &(helpee->deq.req);
	uint64_t s_pending = r->state.pending;
	uint64_t s_idx = r->state.idx;
	uint64_t id = r->id;
	
	if(!s_pending || s_idx < id) {
		return;
	}
	
	wf_segment_t* ha = helpee->head;
	h->hzdp = ha;
	s_idx = r->state.idx;
	s_pending = r->state.pending;
	uint64_t prior = id;
	uint64_t i = id;
	uint64_t cand = 0;
	
	while(true) {
		
		wf_segment_t* hc = NULL;
		for (hc = ha; !cand && s_idx == prior; ) {
			
			cell_t* c = wf_find_cell(&hc, ++i);
			// TODO
			void* v = wf_help_enq(q, h, c, i);
			
			if(v == EMPTY || (v != HEAD_CONST_VAL && c->deq == TAIL_DEQ_VAL)) {
				cand = i;
			} else {
				s_idx = r->state.idx;
				s_pending = r->state.pending;
			}
		}
		
		if(cand) {
			
			CAS_U64_bool((uint64_t*)&(r->state), (0x8000000000000000 | prior), (0x8000000000000000 | cand));
			s_pending = r->state.pending;
			s_idx = r->state.idx;
		}
		
		if(!s_pending || r->id != id) {
			return;
		}
		
		cell_t* c = wf_find_cell(&ha, s_idx);
		
		if(c->val == HEAD_CONST_VAL || CAS_U64_bool(&(c->deq), TAIL_DEQ_VAL, r) || c->deq == r) {
			CAS_U64_bool((uint64_t*) &(r->state), ((s_pending << 63) | s_idx), (0x7FFFFFFFFFFFFFFF & s_idx));
			return;
		}
		
		prior = s_idx;
		
		if(s_idx >= i) {
			cand = 0;
			i = s_idx;
		}
	}
}

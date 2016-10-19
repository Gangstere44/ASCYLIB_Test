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

wf_segment_t * wf_new_segment(uint64_t id) {

	wf_segment_t* new_seg = malloc(sizeof(wf_segment_t));
	new_seg->id = id;
	new_seg->next = NULL;

	uint64_t i;
	for(i = 0; i < SEG_LENGTH; i++) {
        cell_t* cell = &(new_seg->cells[i]);
        cell->val =  TAIL_CONST_VAL;
        cell->enq =  TAIL_ENQ_VAL;
        cell->deq =  TAIL_DEQ_VAL;
	}

	return new_seg;
}

cell_t* wf_find_cell(wf_segment_t* volatile * sp, uint64_t cell_id) {

	wf_segment_t* s = *sp;

	uint64_t i;
	for(i = s->id; i < (cell_id/SEG_LENGTH); i++) {

		wf_segment_t* next = s->next;

		if(next == NULL) {

			wf_segment_t* tmp = wf_new_segment(i + 1);
			if(!CAS_U64_bool(&s->next, NULL, tmp)) {

				free(tmp);
			}

		next = s->next;

		}

		s = next;
	}

	*sp = s;
	return &(s->cells[cell_id % SEG_LENGTH]);
}

void wf_cleanup(wf_queue_t* q, wf_handle_t* h) {
	uint64_t i = q->I;
	wf_segment_t* e = h->head;
	if(i = -1) {
		return;
	}
	if(e->id - i < MAX_GARBAGE(1)) {
		return;
	}
	if(!CAS_U64_bool(&q->I, i, -1)) {
		return;
	}
	uint64_t s = q->tailQ;
	wf_handle_t* hds[q->num_thr]; 
	uint64_t j = 0;
	wf_handle_t* p = NULL;
	for(p = h->next; p != h && e->id > i; p = p->next) {
		wf_verify(&e, p->hzdp);
		wf_update(&p->head, &e, p);
		wf_update(&p->tail, &e, p);
		hds[j++] = p;
	}
	
	while(e->id > i && j > 0) {
		wf_verify(&e, hds[--j]->hzdp);
	}
	
	if(e->id <= i) {
		q->tailQ = s;
		return;
	}
	
	q->tailQ = e;
	q->I = e->id;
	
	wf_free_list(s, e);
}

void wf_free_list(wf_segment_t* from, wf_segment_t* to) {
	
	wf_segment_t* i = NULL;
	for(i = from; i != to; ) {
		wf_segment_t* tmp = i;
		i = i->next;
		free(tmp);
	}
	
}

void wf_update(wf_segment_t* volatile * from, wf_segment_t** to, wf_handle_t* h) {
	wf_segment_t* n = *from;
	if(n->id < (*to)->id) {
		if(!CAS_U64_bool(from, n, *to)) {
			n = *from;
			if(n->id < (*to)->id) {
				*to = n;
			}
		}
		wf_verify(to, h->hzdp);
	}
}

void wf_verify(wf_segment_t** seg, wf_segment_t* hzdp) {
	if(hzdp && hzdp->id < (*seg)->id) {
		*seg = hzdp;
	}
}

void wf_advance_end_for_linearizability(uint64_t* E, uint64_t cid) {

	uint64_t e;
	do {
		e = *E;
	} while(e < cid && !CAS_U64_bool(E, e, cid));
}


wf_queue_t* init_wf_queue(uint64_t num_thr) {

	wf_queue_t* queue = malloc(sizeof(wf_queue_t));
	queue->num_thr = num_thr;
	queue->q = wf_new_segment(0);
	queue->tailQ = 0;
	queue->headQ = 0;
	queue->I = 0;

	return queue;
}

void init_wf_handle(wf_handle_t* handle, wf_segment_t* init_seg) {
	
		handle->head = init_seg;
		handle->tail = init_seg;
		handle->next = NULL;
		handle->hzdp = NULL;
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
}

uint64_t wf_queue_size(wf_queue_t* q) {

	return q->tailQ - q->headQ;	
}

bool wf_queue_contain(wf_queue_t* q, void* val) {
	
	uint64_t currentPos = 0;
	for(currentPos = q->headQ; currentPos <= q->tailQ; currentPos++) {
		
		cell_t* c = wf_find_cell(&(q->q), currentPos);
		if(val == c->val) {
			return true;
		}
	}
	
	return false;
}

// Enqueue functions

void wf_enqueue(wf_queue_t* q, wf_handle_t* h, void* v) {
	h->hzdp = h->tail;
	uint64_t cell_id = 0;
	uint64_t p;
	for(p = 0; p < PATIENCE; p++) {
		if(wf_enq_fast(q, h, v, &cell_id)) {
			return;
		}
	}
	wf_enq_slow(q, h, v, cell_id);
	h->hzdp = NULL;
}

bool wf_try_to_claim_req(uint64_t* s, uint64_t id, uint64_t cell_id) {
	return CAS_U64_bool(s, (0x8000000000000000 | id), (0x7FFFFFFFFFFFFFFF & cell_id));
}

void wf_enq_commit(wf_queue_t* q, cell_t* c, void* v, uint64_t cid) {

	wf_advance_end_for_linearizability(&(q->tailQ), cid + 1);
	c->val = v;
}

bool wf_enq_fast(wf_queue_t* q, wf_handle_t* h, void* v, uint64_t* cid) {
	uint64_t i = FAI_U64(&(q->tailQ));
	cell_t* c = wf_find_cell(&(h->tail), i);
	if(CAS_U64_bool(&(c->val), TAIL_CONST_VAL, v)) {
		return true;
	}

	*cid = i;
	return false;
}

void wf_enq_slow(wf_queue_t* q, wf_handle_t* h, void* v, uint64_t cell_id) {

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

void* wf_help_enq(wf_queue_t* q, wf_handle_t* h, cell_t* c, uint64_t i) {
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

void* wf_dequeue(wf_queue_t* q, wf_handle_t* h) {
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
	}
	
	if(v != EMPTY) {
		wf_help_deq(q, h, (wf_handle_t*) h->deq.peer);
		h->deq.peer = ((wf_handle_t*) h->deq.peer)->next;
	}

	h->hzdp = NULL;
	wf_cleanup(q, h);
	
	return v;
}

void* wf_deq_fast(wf_queue_t* q, wf_handle_t* h, uint64_t* id) {
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

void* wf_deq_slow(wf_queue_t* q, wf_handle_t* h, uint64_t cid) {
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

void wf_help_deq(wf_queue_t* q, wf_handle_t* h, wf_handle_t* helpee) {
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

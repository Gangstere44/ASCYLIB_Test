

#include "queue-waitfree.h"

// present in order to use test_simple.c 
__thread ssmem_allocator_t* alloc;

/* HELPER FUNCTIONS */

wf_queue_t* init_wf_queue(uint64_t num_thr) {

	wf_queue_t* queue = malloc(sizeof(wf_queue_t));
	queue->num_thr = num_thr;
	queue->q = wf_new_segment(0);
	queue->tailQ = 0;
	queue->headQ = 0;
	queue->I = 0;

	#ifdef CHECK_CORRECTNESS
	queue->tot_sum_enq = 0;
	queue->tot_sum_deq = 0;
	#endif

	#ifdef RECORD_F_S
	queue->tot_fast_enq = 0;
	queue->tot_slow_enq = 0;
	queue->tot_fast_deq = 0;
	queue->tot_slow_deq = 0;
	#endif

	return queue;
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

				/* another thread succeeded, we need 
				to reclaim our segment */
				free(tmp);
			}

		next = s->next;

		}

		s = next;

	}

	// we return the segment too
	*sp = s;

	return &(s->cells[cell_id % SEG_LENGTH]);
}

void wf_advance_end_for_linearizability(uint64_t volatile * E, uint64_t cid) {

	/* try to put cid in E as long as the value in E is not cid
	or a bigger integer*/
	uint64_t e;
	do {
		e = *E;
	} while(e < cid && !CAS_U64_bool(E, e, cid));
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

void wf_reclaim_records(wf_queue_t* q, wf_handle_t* h) {

	#ifdef RECORD_F_S
	while(!CAS_U64_bool(&q->tot_fast_enq, q->tot_fast_enq, (q->tot_fast_enq + h->fast_enq)));

	while(!CAS_U64_bool(&q->tot_slow_enq, q->tot_slow_enq, (q->tot_slow_enq + h->slow_enq)));

	while(!CAS_U64_bool(&q->tot_fast_deq, q->tot_fast_deq, (q->tot_fast_deq + h->fast_deq)));

	while(!CAS_U64_bool(&q->tot_slow_deq, q->tot_slow_deq, (q->tot_slow_deq + h->slow_deq)));
	#endif
}

void wf_reclaim_correctness(wf_queue_t* q, wf_handle_t* h) {

	#ifdef CHECK_CORRECTNESS
	while(!CAS_U64_bool(&q->tot_sum_enq, q->tot_sum_enq, (q->tot_sum_enq + h->sum_enq)));

	while(!CAS_U64_bool(&q->tot_sum_deq, q->tot_sum_deq, (q->tot_sum_deq + h->sum_deq)));
	#endif
}

void wf_sum_queue(wf_queue_t* q) {

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

}

/* ENQUEUE FUNCTION */

void wf_enqueue(wf_queue_t* q, wf_handle_t* h, void* v) {

	#ifdef CHECK_CORRECTNESS
	h->sum_enq += v;
	#endif

	/* our hazard pointer is our tail for now */
	h->hzdp = h->tail;
	uint64_t cell_id = 0;
	uint64_t p;
	for(p = 0; p < PATIENCE; p++) {
		if(wf_enq_fast(q, h, v, &cell_id)) {

			#ifdef RECORD_F_S
			h->fast_enq++;
			#endif

			// enq fast succeeds
			h->hzdp = NULL;
			return;
		}
	}

	wf_enq_slow(q, h, v, cell_id);

	#ifdef RECORD_F_S
	h->slow_enq++;
	#endif

	/* we aren't in the queue anymore */
	h->hzdp = NULL;
}

bool wf_try_to_claim_req(uint64_t* s, uint64_t id, uint64_t cell_id) {
	/* old value in s was (1, id) meaning it was pending with id 
		new value will be (0, cell_id) meaning we respond to the request with cell_id.
	Note that bits higher bit is pending bit. */
	return CAS_U64_bool(s, (0x8000000000000000 | id), (0x7FFFFFFFFFFFFFFF & cell_id));
}

void wf_enq_commit(wf_queue_t* q, cell_t* c, void* v, uint64_t cid) {

	/* It first makes sure the head is at least as big as the cell */
	wf_advance_end_for_linearizability(&(q->tailQ), cid + 1);
	c->val = v;
}

bool wf_enq_fast(wf_queue_t* q, wf_handle_t* h, void* v, uint64_t* cid) {

	// get a cell number and go look for it
	uint64_t i = FAI_U64(&(q->tailQ));
	cell_t* c = wf_find_cell(&(h->tail), i);

	// tries to enqueue it
	if(CAS_U64_bool(&(c->val), TAIL_CONST_VAL, v)) {
		return true;
	}

	// fail, but the cell could still be used by another thread to help us
	*cid = i;
	return false;
}

void wf_enq_slow(wf_queue_t* q, wf_handle_t* h, void* v, uint64_t cell_id) {

	// we add a request to ourselves, our peers are now able to help us
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
	
	/* the req isn't pending anymore => a valid cell_id is
	present in the req */
	uint64_t id = r->state.id;
	cell_t* c = wf_find_cell(&(h->tail), id);
	
	wf_enq_commit(q, c, v, id);
}

void* wf_help_enq(wf_queue_t* q, wf_handle_t* h, cell_t* c, uint64_t i) {

	// if the value in the cell is valid, no need to help
	if(!CAS_U64_bool(&(c->val), TAIL_CONST_VAL, HEAD_CONST_VAL) 
		&& c->val != (void*) HEAD_CONST_VAL) {

		return c->val;
	}
	
	// c->val is HEAD_CONST_VAL => help a slow-path enq
	wf_handle_t* volatile p;
	wf_enq_request_t* r;
	if(c->enq == TAIL_ENQ_VAL) { // no enq req in c yet
			
		do {
			
			p = h->enq.peer;
			r = &(p->enq.req);

			/* break if I haven't helped this peer complete */
			if(h->enq.id == 0 || h->enq.id == r->state.id) {
				break;
			}
			
			/* peer request completed => move to next peer */
			h->enq.id = 0;
			h->enq.peer = p->next;
			
		} while(1);
		
		/* if peer enqueue is pending and can use this cell 
		try to reserve this cell by noting request in cell */
		if(r->state.pending && r->state.id <= i && !CAS_U64_bool(&(c->enq), TAIL_ENQ_VAL, r)) {
			// fail to reserve, remember req id
			h->enq.id = r->state.id;
		} else {
			// peer doesn't need help, I can't helped or I helped
			h->enq.peer = p->next;
		}
		
		/* if can't find a pending request, write a value to avoid
		other enq helper to use this cell */
		if(c->enq == TAIL_ENQ_VAL) {
			CAS_U64_bool(&(c->enq), TAIL_ENQ_VAL, HEAD_ENQ_VAL);
		}
	}

	/* invariant : cell's enq is either a req or HEAD_ENQ_VAL */
	if(c->enq == HEAD_ENQ_VAL) {

		/* EMPTY if not enough enqueues linearized before i */
		return (q->tailQ <= i ? EMPTY : HEAD_CONST_VAL);
	}
	
	/* invariant : cell's enq is a request */
	r = c->enq;
	
	if(r->state.id > i) {
		/* request is unsuitable for this cell
		EMPTY if not enough enqueues linearized before i */
		if(c->val == HEAD_CONST_VAL && q->tailQ <= i) {
			return EMPTY;
		}
	} else if (wf_try_to_claim_req((uint64_t*) &(r->state), r->state.id, i) ||
					/* someone claimed this request => not committed */
					((r->state.pending == 0 && r->state.id == i) && c->val == HEAD_CONST_VAL)) {
		wf_enq_commit(q, c, r->val, i);
	}
	
	// c-> val is a val or HEAD_CONST_VAL
	return c->val;
}

// dequeue functions

void* wf_dequeue(wf_queue_t* q, wf_handle_t* h) {

	/* set hazard pointer to head as we are 
	dequeuing */
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
	
	/* invariant : v is a value or EMPTY */
	if(v != EMPTY) {
		/* we got a value => we help a peer */
		wf_help_deq(q, h, (wf_handle_t*) h->deq.peer);
		/* we will help the next peer next time */
		h->deq.peer = ((wf_handle_t*) h->deq.peer)->next;
	}


	#ifdef CHECK_CORRECTNESS
	h->sum_deq += v;
	#endif

	/* not in the queue => hazard pointer to NULL */
	h->hzdp = NULL;
	wf_cleanup(q, h);
	
	return v;
}

void* wf_deq_fast(wf_queue_t* q, wf_handle_t* h, uint64_t* id) {

	/* obtain cell index and locate cnadiate cell */
	uint64_t i = FAI_U64(&(q->headQ));
	cell_t* c = wf_find_cell(&(h->head), i);
	/* help a enqueuer for this cell if possible */
	void* v = wf_help_enq(q, h, c, i);
	
	if(v == EMPTY) {
		return EMPTY;
	}
	
	/* the cell has a value and I claimed it */
	if(v != HEAD_CONST_VAL && 
		CAS_U64_bool(&(c->deq), TAIL_DEQ_VAL, HEAD_DEQ_VAL)) {
		return v;
	}
	
	/* otherwise fail => return cell id */
	*id = i;
	
	return HEAD_CONST_VAL;
}

void* wf_deq_slow(wf_queue_t* q, wf_handle_t* h, uint64_t cid) {

	// publish a dequeue request
	wf_deq_request_t* r = &(h->deq.req);
	r->id = cid;
	r->state.idx = cid;
	r->state.pending = 1;
	
	wf_help_deq(q, h, h);
	
	/* find the destination cell and read its value */
	uint64_t i = r->state.idx;
	cell_t* c = wf_find_cell(&(h->head), i);
	void* v = c->val;
	
	/* need to make sure the head of the queue is at least
	higher than the cell we dequeued */
	wf_advance_end_for_linearizability(&(q->headQ), i + 1);
	
	return (v == HEAD_CONST_VAL ? EMPTY : v);
	
}

void wf_help_deq(wf_queue_t* q, wf_handle_t* h, wf_handle_t* helpee) {

	// inspect a dequeue request
	wf_deq_request_t* r = &(helpee->deq.req);
	uint64_t s_pending = r->state.pending;
	uint64_t s_idx = r->state.idx;
	uint64_t id = r->id;
	
	/* if this request doesn't need help, return */
	if(!s_pending || s_idx < id) {
		return;
	}
	
	/* ha a local segment pointer for announced cells */
	wf_segment_t* ha = helpee->head;
	h->hzdp = ha;
	/* must read r after reading helpee->head */
	s_idx = r->state.idx;
	s_pending = r->state.pending;
	uint64_t prior = id;
	uint64_t i = id;
	uint64_t cand = 0;
	
	while(true) {
		/* find a candidate cell, if I don't have one.
		loop breaks when either find a candidate
		or a candidate is announced.
		hc: a locale segment pointer for candidate cells */
		wf_segment_t* hc = NULL;
		for (hc = ha; !cand && s_idx == prior; ) {
			
			/* we get cell with higher id than the one
			in the deq request */
			cell_t* c = wf_find_cell(&hc, ++i);
			void* v = wf_help_enq(q, h, c, i);
			
			/* it is a cnadidate if it help_enq return EMPTY
			or a value that is not claimed by dequeues */
			if(v == EMPTY || (v != HEAD_CONST_VAL && c->deq == TAIL_DEQ_VAL)) {
				cand = i;
			} else {
				// inspect request state again
				s_idx = r->state.idx;
				s_pending = r->state.pending;
			}
		}
		
		if(cand) {
			// found a candidate cell => try to announce it
			CAS_U64_bool((uint64_t*)&(r->state), (0x8000000000000000 | prior), (0x8000000000000000 | cand));
			s_pending = r->state.pending;
			s_idx = r->state.idx;
		}
		
		/* invariant: some candidate announced in s.idx
		quit if request is complete */
		if(!s_pending || r->id != id) {
			return;
		}
		
		// find the announced candidate
		cell_t* c = wf_find_cell(&ha, s_idx);
		
		/* if candidate permits returning EMPTY (c->val = HEAD_CONST_VAL)
		or this helper claimed the value for r with CAS
		or another helper claimed the value for r */
		if(c->val == HEAD_CONST_VAL || 
			CAS_U64_bool(&(c->deq), TAIL_DEQ_VAL, r) || 
			c->deq == r) {
			
			/* request is complete, try to clear pending bit */
			CAS_U64_bool((uint64_t*) &(r->state), ((s_pending << 63) | s_idx), (0x7FFFFFFFFFFFFFFF & s_idx));
			return;
		}
		
		// prepare for next iteration
		prior = s_idx;
		
		/* if announced candidate is newer than visited cell
		abandon "cand" (if any); bump i */
		if(s_idx >= i) {
			cand = 0;
			i = s_idx;
		}
	}
}

void wf_cleanup(wf_queue_t* q, wf_handle_t* h) {
	
	uint64_t i = q->I;
	wf_segment_t* e = h->head;

	// if somebody else is already cleaning => return
	if(i == -1) {
		return;
	}

	// if the number of segment to clean isn't enough => return
	if(e->id - i < MAX_GARBAGE(2)) {
		return;
	}

	// we try to get access to this section
	if(!CAS_U64_bool(&q->I, i, -1)) {
		return;
	}

	wf_segment_t* s = q->q;
	wf_handle_t* hds[q->num_thr]; 
	uint64_t j = 0;
	wf_handle_t* p = NULL;

	for(p = h->next; p != h && e->id > i; p = p->next) {
		/* will move the head and tail 
		of the handles to the smallest seg in use,
		this seg is given by the hazard pointer of
		the different handle*/

		wf_verify(&e, p->hzdp); 
		wf_update(&p->head, &e, p);
		wf_update(&p->tail, &e, p);
		hds[j++] = p;
	}

	/* reverse traversal because the last update
	we made before was the best in any case */
	while(e->id > i && j > 0) {
		wf_verify(&e, hds[--j]->hzdp);
	}

	/* if the best new segment position is
	smaller or the last freed segment 
	=> nothing to do */
	if(e->id <= i) {
		q->q = s;
		q->I = i;
		return;
	}
	
	/* a better position found*/
	q->q = e;
	q->I = e->id;
	
	/* we free from s to e, e not include */
	wf_free_list(s, e);

}

void wf_free_list(wf_segment_t* from, wf_segment_t* to) {
	
	wf_segment_t* i = NULL;
	for(i = from; i != to; ) {
		wf_segment_t* tmp = i;
		i = i->next;
		//free(tmp);
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

void wf_verify(wf_segment_t** seg, wf_segment_t* volatile hzdp) {

	if(hzdp && hzdp->id < (*seg)->id) {
		*seg = hzdp;
	}
}
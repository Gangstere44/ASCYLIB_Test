

#include "wait_free_stack.h"

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

#  define START_TS_ALLOC()				\
    asm volatile ("");				\
    ticks start_acq_alloc = getticks();			\
    LFENCE;
#  define END_TS_ALLOC()				\
    asm volatile ("");				\
    ticks end_acq_alloc = getticks();			\
    asm volatile ("");
#  define ADD_DUR_ALLOC(tar) tar += (end_acq_alloc - start_acq_alloc - correction)

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
	new_stack->sentinel.push_tid = -1; 
	new_stack->top = &new_stack->sentinel;
	
	new_stack->announces = calloc(num_thr, sizeof(push_op_t*));
	new_stack->all_delete_requests = calloc(num_thr, sizeof(delete_req_t*));
	uint64_t i;
	for(i = 0; i < num_thr; i++) {
		new_stack->announces[i] = NULL;
		new_stack->all_delete_requests[i] = NULL;
	}

	new_stack->thr_info = calloc(num_thr, sizeof(thr_latencies_t));
	for(i = 0; i < num_thr; i++) {
		memset(&new_stack->thr_info[i], 0, sizeof(thr_latencies_t));
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
	
printf("stack size \n");
	uint64_t n_elem = 0;
	node_t* tmp = s->top;

	volatile uint64_t rem = 0;
	
	while(tmp->push_tid != -1) {
		rem++;
		if(!tmp->mark) {
			n_elem++;
		}

		tmp = tmp->prev;	
	}
	
	printf("\n\n stack total : %lu \n\n", rem);

	return n_elem;
}

void push(wf_stack_t* s, int64_t tid, void* value) {

	START_TS();

	uint64_t new_phase = FAI_U64(&s->phase_counter_push_req);

	if(s->announces[tid] != NULL) {
		START_TS_ALLOC();
		ssmem_free(alloc_wf, (void*) s->announces[tid]);
		END_TS_ALLOC();
		ADD_DUR_ALLOC(s->thr_info[tid].ssmem_free_lat);
		s->thr_info[tid].ssmem_free_count++;
	}

	START_TS_ALLOC();
	s->announces[tid] = init_push_op(new_phase, init_node(value, tid));
	END_TS_ALLOC();
	ADD_DUR_ALLOC(s->thr_info[tid].ssmem_alloc_lat);
	s->thr_info[tid].ssmem_alloc_count++;

	help(s, s->announces[tid], tid);

	END_TS();
	ADD_DUR(s->thr_info[tid].push_lat);
	s->thr_info[tid].push_count++;
}

void help(wf_stack_t* s, push_op_t* request, int64_t tid) {

	START_TS();

	push_op_t* min_req = NULL;
	
	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		
		push_op_t* tmp = s->announces[i];
		if(tmp != NULL && !tmp->pushed && 
			 (min_req == NULL || min_req->phase > tmp->phase)) {
			min_req = tmp;
		}
	}

	if(min_req == NULL || min_req->phase > request->phase) {

		END_TS();
		ADD_DUR(s->thr_info[tid].help_lat);
		s->thr_info[tid].help_count++;

		return;
	}

	attach_node(s, min_req, tid);

	if(min_req != request) {

		attach_node(s, request, tid);
	}

	END_TS();
	ADD_DUR(s->thr_info[tid].help_lat);
	s->thr_info[tid].help_count++;

}

void attach_node(wf_stack_t* s, push_op_t* request, int64_t tid) {

	START_TS();

	while(!request->pushed) {

		node_t* last = s->top;

		node_t* next = last->next_done;

		if(last == s->top) {



			if(next == NULL ){

				if(!request->pushed) {

					node_t* node_req = request->node;				

					if(CAS_U64_bool(&last->next_done, NULL, node_req)) {

						update_top(s, tid);

						CAS_U64_bool(&last->next_done, node_req, (void*) 1);
		
						END_TS();
						ADD_DUR(s->thr_info[tid].attach_node_lat);
						s->thr_info[tid].attach_node_count++;

						return;
					}
				}
			}
			update_top(s, tid);
		}		
	}

	END_TS();
	ADD_DUR(s->thr_info[tid].attach_node_lat);
	s->thr_info[tid].attach_node_count++;
	
}

void update_top(wf_stack_t* s, int64_t tid) {

	START_TS();

	node_t* last = s->top;
	node_t* next = last->next_done;

	if(next != NULL && next != ((void*) 1)) {

		push_op_t* request = s->announces[next->push_tid];
		
		if(last == s->top && request->node == next) {

			CAS_U64_bool(&next->prev, NULL, last); 
		
			next->index = last->index + 1;
			request->pushed = true;
			bool stat = CAS_U64_bool(&s->top, last, next);

		
			if(next->index % W == 0 && stat) {

				try_clean_up(s, next, tid, true);
			}
		}
	}

	END_TS();
	ADD_DUR(s->thr_info[tid].update_top_lat);
	s->thr_info[tid].update_top_count++;
	
}

node_t* pop(wf_stack_t* s, int64_t tid) {

	START_TS();

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

		END_TS();
		ADD_DUR(s->thr_info[tid].pop_lat);
		s->thr_info[tid].pop_count++;

		return EMPTY_STACK;
	}

	void* result = cur->value;

	try_clean_up(s, cur, tid, false);
	s->thr_info[tid].try_clean_up_count2++;


	END_TS();
	ADD_DUR(s->thr_info[tid].pop_lat);
	s->thr_info[tid].pop_count++;

	return result;
}

void try_clean_up(wf_stack_t* s, node_t* n, int64_t tid, bool from_right_node) {

	START_TS();

	node_t* tmp = NULL;

	if(from_right_node) {
		tmp = n->prev;
	} else {
		tmp = n;
	}

	while(tmp->push_tid != -1) {

		if(tmp->index % W == 0) {

			if(IAF_U64(&tmp->counter) == W + 1) {

				clean(s, tid, tmp); 
			} 
			break;
		}

		tmp = tmp->prev;

		tmp->prev;

	}

	END_TS();
	if(from_right_node) {
		ADD_DUR(s->thr_info[tid].try_clean_up_lat1);
		s->thr_info[tid].try_clean_up_count1++;
	} else {
		ADD_DUR(s->thr_info[tid].try_clean_up_lat2);
		s->thr_info[tid].try_clean_up_count2++;
	}
}

void clean(wf_stack_t* s, int64_t tid, node_t* n) {

	START_TS();

	if(s->all_delete_requests[tid] != NULL) {
		START_TS_ALLOC();
		ssmem_free(alloc_wf, (void*) s->all_delete_requests[tid]);
		END_TS_ALLOC();
		ADD_DUR_ALLOC(s->thr_info[tid].ssmem_free_lat);
		s->thr_info[tid].ssmem_free_count++;
	}

	uint64_t phase = FAI_U64(&s->phase_counter_del_req);

	START_TS_ALLOC();
	s->all_delete_requests[tid] = init_delete_req(phase, tid, n);
	END_TS_ALLOC();
	ADD_DUR_ALLOC(s->thr_info[tid].ssmem_alloc_lat);
	s->thr_info[tid].ssmem_alloc_count++;

	help_delete(s, s->all_delete_requests[tid], tid);

	END_TS();
	ADD_DUR(s->thr_info[tid].clean_lat);
	s->thr_info[tid].clean_count++;
}

void help_delete(wf_stack_t* s, delete_req_t* dr, int64_t tid) {

	START_TS();

	delete_req_t* min_req = NULL;
	
	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {
		
		delete_req_t* tmp = s->all_delete_requests[i];
		if(tmp != NULL && tmp->pending && 
			(min_req == NULL || min_req->phase > tmp->phase)) {
			min_req = tmp;
		}
	}
	
	if(min_req == NULL || min_req->phase > dr->phase) {

		END_TS();
		ADD_DUR(s->thr_info[tid].help_delete_lat);
		s->thr_info[tid].help_delete_count++;

		return;
	}
	
	unique_delete(s, min_req, tid);

	if(min_req != dr) {
		unique_delete(s, dr, tid);
	}

	END_TS();
	ADD_DUR(s->thr_info[tid].help_delete_lat);
	s->thr_info[tid].help_delete_count++;
}

void unique_delete(wf_stack_t* s, delete_req_t* dr, int64_t tid) {

	START_TS();

	while(dr->pending) {

		delete_req_t* cur = s->unique_req;

		if(cur == NULL || !cur->pending) {

			if(dr->pending) {

				bool stat = dr != cur ? CAS_U64_bool(&s->unique_req, cur, dr) : true;

				help_finish_delete(s, tid);
				if(stat) {
					
					END_TS();
					ADD_DUR(s->thr_info[tid].unique_delete_lat);
					s->thr_info[tid].unique_delete_count++;

					return;
				} 
			}

		} else {

			help_finish_delete(s, tid);
		}
	}

	END_TS();
	ADD_DUR(s->thr_info[tid].unique_delete_lat);
	s->thr_info[tid].unique_delete_count++;
}

void help_finish_delete(wf_stack_t* s, int64_t tid) {

	START_TS();

	delete_req_t* cur = s->unique_req;
	
	if(!cur->pending) {

		END_TS();
		ADD_DUR(s->thr_info[tid].help_finish_delete_lat);
		s->thr_info[tid].help_finish_delete_count++;

		return;
	}
	
	uint64_t end_idx = cur->node->index + W - 1;
	node_t* right_node = s->top;
	node_t* left_node = right_node->prev;
	
	while(left_node->index != end_idx && left_node->push_tid != -1) {

		right_node = left_node;
		left_node = left_node->prev;
	}
	
	if(left_node->push_tid == -1) {

		END_TS();
		ADD_DUR(s->thr_info[tid].help_finish_delete_lat);
		s->thr_info[tid].help_finish_delete_count++;

		return;
	}
	
	node_t* target = left_node;
	uint64_t i;
	for(i = 0; i < W; i++) {

		target = target->prev;
	}

	if(CAS_U64_bool(&right_node->prev, left_node, target)) {

		START_TS_ALLOC();
		node_t* tmp;
		for(i = 0; i < W; i++) {
			tmp = left_node;
			left_node = left_node->prev;

			ssmem_free(alloc_wf, (void*) tmp);
		}
		END_TS_ALLOC();
		ADD_DUR_ALLOC(s->thr_info[tid].ssmem_free_lat);
		s->thr_info[tid].ssmem_free_count++;

	}
	
	cur->pending = false;

	END_TS();
	ADD_DUR(s->thr_info[tid].help_finish_delete_lat);
	s->thr_info[tid].help_finish_delete_count++;

}

void print_latency(wf_stack_t* s, int64_t tid) {

	thr_latencies_t* i = &s->thr_info[tid];
	printf("Latency result for thread %ld \n \
		push -> %lu \t help -> %lu \t attach_node -> %lu \t update_top -> %lu \n \
		pop -> %lu \t try_clean_up1 -> %lu \t help_finish_delete -> %lu \t clean -> %lu \t help_delete -> %lu \t	unique_del -> %lu \n ",
		tid, i->push_lat, i->help_lat, i->attach_node_lat, i->update_top_lat, i->pop_lat, i->try_clean_up_lat1,
		i->help_finish_delete_lat, i->clean_lat, i->help_delete_lat, i->unique_delete_lat);

}

void print_latencies(wf_stack_t* s) {

	thr_latencies_t t = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	uint64_t i;
	for(i = 0; i < s->num_thr; i++) {

		thr_latencies_t* tmp = &s->thr_info[i];

		t.push_lat += tmp->push_lat;
		t.push_count += tmp->push_count;

		t.help_lat += tmp->help_lat;
		t.help_count += tmp->help_count;

		t.attach_node_lat += tmp->attach_node_lat;
		t.attach_node_count += tmp->attach_node_count;

		t.update_top_lat += tmp->update_top_lat;
		t.update_top_count += tmp->update_top_count;

		t.pop_lat += tmp->pop_lat;
		t.pop_count += tmp->pop_count;

		t.try_clean_up_lat1 += tmp->try_clean_up_lat1;
		t.try_clean_up_count1 += tmp->try_clean_up_count1;

		t.try_clean_up_lat2 += tmp->try_clean_up_lat2;
		t.try_clean_up_count2 += tmp->try_clean_up_count2;

		t.help_finish_delete_lat += tmp->help_finish_delete_lat;
		t.help_finish_delete_count += tmp->help_finish_delete_count;

		t.clean_lat += tmp->clean_lat;
		t.clean_count += tmp->clean_count;

		t.help_delete_lat += tmp->help_delete_lat;
		t.help_delete_count += tmp->help_delete_count;

		t.unique_delete_lat += tmp->unique_delete_lat;
		t.unique_delete_count += tmp->unique_delete_count;

		t.ssmem_alloc_lat += tmp->ssmem_alloc_lat;
		t.ssmem_alloc_count += tmp->ssmem_alloc_count;

		t.ssmem_free_lat += tmp->ssmem_free_lat;
		t.ssmem_free_count += tmp->ssmem_free_count;
	}

	
	t.push_lat -= t.help_lat;
	t.help_lat -= t.attach_node_lat;
	t.attach_node_lat -= t.update_top_lat;
	t.update_top_lat -= t.try_clean_up_lat1;

	t.pop_lat -= t.try_clean_up_lat2;
	t.try_clean_up_lat1 += t.try_clean_up_lat2;
	t.try_clean_up_lat1 -= t.clean_lat;
	t.clean_lat -= t.help_delete_lat;
	t.help_delete_lat -= t.unique_delete_lat;
	t.unique_delete_lat -= t.help_finish_delete_lat;
	

	t.push_lat /= t.push_count;
	t.help_lat /= t.help_count;
	t.attach_node_lat /= t.attach_node_count;
	t.update_top_lat /= t.update_top_count;

	t.pop_lat /= t.pop_count;
	//t.try_clean_up_lat1 += t.try_clean_up_lat2;
	t.try_clean_up_lat1 /= (t.try_clean_up_count1 + t.try_clean_up_count2);
	t.clean_lat /= t.clean_count;
	t.help_delete_lat /= t.help_delete_count;
	t.unique_delete_lat /= t.unique_delete_count;
	t.help_finish_delete_lat /= t.help_finish_delete_count;

	t.ssmem_alloc_lat /= t.ssmem_alloc_count;
	t.ssmem_free_lat /= t.ssmem_free_count;

	printf("Latencies result for all threads \n push -> %lu \n help -> %lu \n attach_node -> %lu \n update_top -> %lu \n pop -> %lu \n try_clean_up -> %lu \n clean -> %lu \n help_delete -> %lu \n unique_del -> %lu \n ",
		t.push_lat, t.help_lat, t.attach_node_lat, t.update_top_lat, t.pop_lat, t.try_clean_up_lat1, t.clean_lat, t.help_delete_lat, t.unique_delete_lat);
	printf("help_finish_delete -> %lu \n", t.help_finish_delete_lat);
	printf("ssmem_alloc -> %lu \n ", t.ssmem_alloc_lat);
	printf("ssmem_free -> %lu \n", t.ssmem_free_lat);
}

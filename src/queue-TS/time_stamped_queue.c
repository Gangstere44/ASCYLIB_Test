
#include "time_stamped_queue.h"

__thread ssmem_allocator_t* alloc_ts;

ts_queue_t* new_ts_queue(int64_t num_thread) {

	ts_queue_t* new_q = malloc(sizeof(ts_queue_t));
	new_q->pools = calloc(num_thread, sizeof(pool_t*));
	uint64_t i;
	for(i = 0; i < num_thread; i++) {
		pools[i] = new_pool(i);
	}
	
	return new_q;
	
}

pool_t* new_pool(int64_t tid) {
	
	pool_t* new_pool = malloc(sizeof(new_pool));
	new_pool->head = NULL;
	new_pool->tail = NULL;
	new_pool->counter_ts_pool = 0;
	
	return new_pool;
}

node_t* new_node(void* val, bool taken) {
	
	node_t* new_node = malloc(sizeof(node_t));
	
	new_node->ts.timestamp = -1;
	new_node->value = val;
	new_node->taken = taken;
	new_node->next = NULL;
	
	return new_node;
}

uint64_t queue_size(ts_queue_t* q) {
	
	uint64_t size = 0;
	
	uint64_t i = 0;
	for(i = 0; i < q->num_thread; i++) {
		
		pool_t* p = pools[i];
		
		node_t* tmp = p->head;
		
		while(tmp != NULL) {
			
			if(!tmp->taken) {
				size++;
			}
			
			tmp = tmp->next;	
		}
	}
	
	return size;
}

void push(ts_queue_t* q, void* val, int64_t tid) {
	
	pool_t* pool = q->pools[tid];
	node_t* n = insert(pool, val);
	n->ts = FAI(&q->counter_ts);
}

void* pop(ts_queue_t* q, int64_t tid) {
	
	time_stamp_t ts = FAI(&q->counter_ts);
	
	pop_request_t pop_req = {.new_tail = NULL, .old_tail = NULL};
		
	do {
		try_remove(q, ts, &pop_req);
	} while(!pop_req->success);
	
	return pop_req->element;
}

void try_remove(ts_queue_t* q, time_stamp_t* start_time, pop_request_t* pop_req) {

	node_t* oldest = NULL;
	time_stamp_t ts = {.timestamp = -1};
	pool_t* pool = NULL;
	node_t* top = NULL;
	node_t** empty = calloc(q->num_thread, sizeof(node_t*));
	
	
	uint64_t i;
	for(i = 0; i < q->num_thread; i++) {
		
		pool_t* current = q->pools[i];

		get_oldest_request_t old_req = {.node = NULL, .pool_top = NULL};
		get_oldest(q, current, &old_req);
		
		if(n == NULL) {
			empty[i] = old_req.pool_top;
		}
		
		time_stamp_t* node_time_stamp = &node->ts;
		
		if(ts.timestamp == -1 || ts.timestamp < node_time_stamp) {
			oldest = old_req.node;
			ts.timestamp = node_time_stamp->timestamp;
			pool = current;
			top = old_req.pool_top;
		}
	}
	
	if(oldest == NULL) {
		for(i = 0; i < q->num_thread; i++) {
			pool_t* current = q->pools[i];
			if(current->head != empty[i]) {
				return;
			}
		}
		
		pop_req->success = true;
		pop_req->element = EMPTY;
		return;
	}
	
	remove(q, top, oldest, pop_req)
}

node_t* insert(ts_queue_t* q, pool_t* pool, void* val) {
	
	node_t* new_node = new_node(val, false);
	pool->head->next = new_node;
	pool->head = new_node;
	
	node_t* next = pool->tail;
	while(next.taken) {
		node_t* tmp = next;
		next = next->next;
		//free(next)
	}
	pool->tail = next;
	
	return new_node;
}

node_t*, node_t* get_oldest(ts_queue_t* q, pool_t* p, get_oldest_request_t* old_req) {

	node_t* old_tail = p->tail;
	node_t* res = old_tail;
	while(true) {
		if(!result->taken) {
			return result, old_tail;
		} else if(result->next == NULL) {
			return NULL, old_tail;
		}
		result = result->next;
	}
	
}

bool, void* remove(ts_queue_t* q, node_t* old_tail, node_t* new_tail, pop_request_t* pop_req) {
	
	if(CAS_U64_bool(&new_tail->taken, false, true)) {
		CAS_U64_bool(&q->tail, old_tail, new_tail);
		
		pop_req->success = true;
		pop_req->element = new_tail->value;
	}
}
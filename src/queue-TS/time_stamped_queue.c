
#include "time_stamped_queue.h"

__thread ssmem_allocator_t* alloc_ts;

ts_queue_t* ts_new_queue(int64_t num_thread) {

	ts_queue_t* new_q = malloc(sizeof(ts_queue_t));

	new_q->num_thread = num_thread;
	new_q->counter_ts = 0;

	new_q->pools = calloc(num_thread, sizeof(pool_t*));
	uint64_t i;
	for(i = 0; i < num_thread; i++) {
		new_q->pools[i] = ts_new_pool(i);
	}
	
	return new_q;
	
}

pool_t* ts_new_pool(int64_t tid) {
	
	pool_t* new_pool = malloc(sizeof(pool_t));
	new_pool->head = NULL;
	new_pool->tail = NULL;
	new_pool->counter_ts_pool = 0;
	
	return new_pool;
}

node_t* ts_new_node(void* val, bool taken) {
	
	node_t* new_node = ssmem_alloc(alloc_ts, sizeof(node_t));
	
	new_node->ts = MAX_TIME_STAMP;
	new_node->value = val;
	new_node->taken = taken;
	new_node->next = NULL;
	
	return new_node;
}

uint64_t ts_queue_size(ts_queue_t* q) {
	
	uint64_t size = 0;
	
	uint64_t remain = 0;

	uint64_t i = 0;
	for(i = 0; i < q->num_thread; i++) {
		
		pool_t* p = q->pools[i];
		
		node_t* tmp = p->head;
		
		while(tmp != NULL) {
			
			if(!tmp->taken) {
				size++;
				remain += tmp->value;
			}
			
			tmp = tmp->next;	
		}
	}

	//printf("remains %lu \n", remain);
	
	return size;
}

void ts_push(ts_queue_t* q, void* val, int64_t tid) {
	
	//printf("push 1 \n");

	pool_t* pool = q->pools[tid];
		//printf("push 2 \n");

	node_t* n = ts_insert(q, pool, val);
	//	printf("push 3 \n");

	n->ts = FAI_U64(&q->counter_ts);
	//	printf("push 4\n");

}

void* ts_pop(ts_queue_t* q, int64_t tid) {
	
	//printf("pop 1\n");

	uint64_t time_stamp = FAI_U64(&q->counter_ts);
	
	pop_request_t pop_req = {.success = false, .element = NULL};
	//		printf("pop 2\n");

	do {
		ts_try_remove(q, time_stamp, &pop_req);
	} while(!pop_req.success);
	//	printf("pop 3\n");

	return pop_req.element;
}

void ts_try_remove(ts_queue_t* q, uint64_t start_time, pop_request_t* pop_req) {

	node_t* new_head = NULL;
	uint64_t time_stamp = MAX_TIME_STAMP;
	pool_t* pool = NULL;
	node_t* head = NULL;
	node_t** empty = calloc(q->num_thread, sizeof(node_t*));
	
	
	uint64_t i;
	for(i = 0; i < q->num_thread; i++) {
		
		pool_t* current = q->pools[i];

		get_oldest_request_t old_req = {.node = NULL, .pool_head = NULL};
		ts_get_oldest(q, current, &old_req);
		
		if(old_req.node == NULL) {
			empty[i] = old_req.pool_head;
			continue;
		}
		
		uint64_t node_time_stamp = old_req.node->ts;
		
		if(time_stamp == MAX_TIME_STAMP || time_stamp < node_time_stamp) {
			new_head = old_req.node;
			time_stamp = node_time_stamp;
			pool = current;
			head = old_req.pool_head;
		}
	}
	
	if(new_head == NULL) {
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
	
	ts_remove(q, pool, head, new_head, pop_req);
}

node_t* ts_insert(ts_queue_t* q, pool_t* pool, void* val) {
	
	node_t* new_node = ts_new_node(val, false);

	if(pool->tail == NULL) {
		pool->tail = new_node;
		pool->head = new_node;
	} else {
		pool->tail->next = new_node;
		pool->tail = new_node;
	}
		
	return new_node;
}

void ts_get_oldest(ts_queue_t* q, pool_t* p, get_oldest_request_t* old_req) {

	node_t* old_head = p->head;
	node_t* result = old_head;
	while(true) {
		if(result != NULL && !result->taken) {
			old_req->node = result;
			old_req->pool_head = old_head;
			return;
		} else if(result == NULL || result->next == NULL) {
			old_req->node = NULL;
			old_req->pool_head = old_head;
			return;
		}
		result = result->next;
	}
}

void ts_remove(ts_queue_t* q, pool_t* pool, node_t* old_head, node_t* new_head, pop_request_t* pop_req) {
	
	if(CAS_U64_bool(&new_head->taken, false, true)) {

/*
		CAS_U64_bool(&pool->head, old_head, new_head);

		pop_req->success = true;
		pop_req->element = new_head->value;
*/

		if(CAS_U64_bool(&pool->head, old_head, new_head)) {
		
			node_t* tmp = old_head;
			node_t* next_tmp = tmp;
			while(tmp->taken && tmp != new_head) {
				next_tmp = tmp->next;
				ssmem_free(alloc_ts, (void*) tmp);
				tmp = next_tmp;
			}

		}

			pop_req->success = true;
			pop_req->element = new_head->value;

	}
}
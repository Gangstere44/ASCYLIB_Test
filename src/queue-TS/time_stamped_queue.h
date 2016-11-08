

#ifndef ASCYLIB_PROJECT_TIME_STAMPED_QUEUE_H
#define ASCYLIB_PROJECT_TIME_STAMPED_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "atomic_ops_if.h"
#include "ssmem.h"

#define EMPTY ((void*) 0)
#define MAX_TIME_STAMP ((uint64_t) -1)

extern __thread ssmem_allocator_t* alloc_ts;

typedef struct node {
	
	void* value;
	uint64_t ts;
	volatile bool taken;
	struct node* volatile next;
	
} node_t;

typedef struct pop_request {
	
	bool success;
	void* element;
	
} pop_request_t;

typedef struct get_oldest_request {
	
	node_t* node;
	node_t* pool_head;

	uint64_t padding[6];

} get_oldest_request_t;

typedef struct pool {
	
	int64_t tid;
	node_t* volatile head;
	node_t* volatile tail;
	
	uint64_t counter_ts_pool;

} pool_t;

typedef struct ts_queue {
	
	pool_t** pools;
	int64_t num_thread;
	uint64_t counter_ts;
	
} ts_queue_t;

ts_queue_t* ts_new_queue(int64_t num_thread);
pool_t* ts_new_pool(int64_t tid);
node_t* ts_new_node(void* val, bool taken);

uint64_t ts_queue_size(ts_queue_t* q);

void ts_push(ts_queue_t* q, void* val, int64_t tid);
void* ts_pop(ts_queue_t* q, int64_t tid);
void ts_try_remove(ts_queue_t* q, uint64_t start_time, pop_request_t* pop_req);
node_t* ts_insert(ts_queue_t* q, pool_t* pool, void* val);
void ts_get_oldest(ts_queue_t* q, pool_t* p, get_oldest_request_t* old_req);
void ts_remove(ts_queue_t* q, pool_t* pool, node_t* old_head, node_t* new_head, pop_request_t* pop_req);

#endif //ASCYLIB_PROJECT_TIME_STAMPED_QUEUE_H

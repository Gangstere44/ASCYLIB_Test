

#ifndef ASCYLIB_PROJECT_TIME_STAMPED_QUEUE_H
#define ASCYLIB_PROJECT_TIME_STAMPED_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "atomic_ops_if.h"
#include "ssmem.h"

#define EMTPY ((void*) 0)

extern __thread ssmem_allocator_t* alloc_ts;

typedef struct time_stamp {
	
	uint64_t timestamp;
	
} time_stamp_t;

typedef struct node {
	
	void* value;
	time_stamp_t ts;
	struct node* volatile next;
	volatile bool taken;
	
} node_t;

typedef struct pop_request {
	
	bool success;
	void* element;
	
} pop_request_t*;

typedef struct get_oldest_request {
	
	node_t* node;
	node_t* pool_top;
	
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

ts_queue_t* new_ts_queue(int64_t num_thread);
pool_t* new_pool(int64_t tid);
node_t* new_node(void* val, bool taken);

uint64_t queue_size(ts_queue_t* q);

void push(ts_queue_t* q, void* val, int64_t tid);
void* pop(ts_queue_t* q, int64_t tid);
void try_remove(ts_queue_t* q, time_stamp_t* start_time, pop_request_t* req);
node_t* insert(ts_queue_t* q, pool_t* pool, void* val);
node_t*, node_t* get_oldest(ts_queue_t* q, pool_t* p);
void remove(ts_queue_t* q, node_t* old_top, node_t* n, pop_request_t* req);


#endif //ASCYLIB_PROJECT_TIME_STAMPED_QUEUE_H



#ifndef QUEUE_TIMESTAMP_H
#define QUEUE_TIMESTAMP_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "atomic_ops_if.h"
#include "ssmem.h"
#include "utils.h"
#include "common.h"

#define EMPTY ((void*) 0)
#define MAX_TIME_STAMP ((uint64_t) -1)
#define DELAY_TIME_TICKS 1

#define TS_NAIVE 1
#define TS_CAS 2
#define TS_INTERVAL 3

extern __thread ssmem_allocator_t* alloc_ts;

typedef struct time_stamp {

	// interval
	uint64_t begin;
	uint64_t end;

	// naive ts
	uint64_t counter_value;

} time_stamp_t;

typedef struct node {
	
	void* value;
	volatile bool taken;
	struct node* volatile next;
	time_stamp_t ts;

	// padding of size 2 to reach 64 bytes
	uint64_t padding[2];
	
} node_t;

typedef struct dequeue_request {
	
	bool success;
	void* element;
	
} dequeue_request_t;

typedef struct get_oldest_request {
	
	node_t* node;
	node_t* pool_head;

} get_oldest_request_t;

typedef struct pool {
	
	int64_t tid;
	node_t* volatile head;
	node_t* volatile tail;
	
} pool_t;

typedef struct ts_queue {
	
	pool_t** pools;
	int64_t num_thread;
	uint64_t counter_ts;
	
	ticks delay_ticks;

	int strategy;

} ts_queue_t;

ts_queue_t* ts_new_queue(int64_t num_thread);
pool_t* ts_new_pool(int64_t tid);
node_t* ts_new_node(void* val, bool taken);

uint64_t ts_queue_size(ts_queue_t* q);

void ts_enqueue(ts_queue_t* q, void* val, int64_t tid);
void ts_interval(ts_queue_t* q, time_stamp_t* ts);
void ts_CAS(ts_queue_t* q, time_stamp_t* ts);
void* ts_dequeue(ts_queue_t* q, int64_t tid);
void ts_try_remove(ts_queue_t* q, time_stamp_t* start_time, dequeue_request_t* pop_req);
node_t* ts_insert(ts_queue_t* q, pool_t* pool, void* val);
void ts_get_oldest(ts_queue_t* q, pool_t* p, get_oldest_request_t* old_req);
void ts_remove(ts_queue_t* q, pool_t* pool, node_t* old_head, node_t* new_head, dequeue_request_t* deq_req);

#endif /* QUEUE_TIMESTAMP_H */

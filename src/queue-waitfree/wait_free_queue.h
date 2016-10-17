//
// Created by Quentin on 02.10.2016.
//

#ifndef ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H
#define ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#define SEG_LENGTH (1 << 10)



typedef struct wf_enq_request
{
	void* val;
	struct
	{
		uint64_t id 		: 63;
		uint64_t pending 	: 1;
	} state;
} wf_enq_request_t;

typedef struct wf_deq_request
{
	uint64_t id;
	struct
	{
		uint64_t idx 		: 63;
		uint64_t pending 	: 1;
	} state;
} wf_deq_request_t;

typedef struct cell
{
	void* val;
	wf_enq_request_t* enq;
	wf_deq_request_t* deq;
} cell_t;

#define TAIL_CONST_VAL ((void*) 1)
#define HEAD_CONST_VAL ((void*) 2)
#define EMPTY ((void*) 3)
#define TAIL_ENQ_VAL   ((wf_enq_request_t*) 1)
#define HEAD_ENQ_VAL   ((wf_enq_request_t*) 2)
#define TAIL_DEQ_VAL   ((wf_deq_request_t*) 1)
#define HEAD_DEQ_VAL   ((wf_deq_request_t*) 2)

typedef struct wf_segment
{
	uint64_t id;
	struct wf_segment* next;
	cell_t cells[SEG_LENGTH];
} wf_segment_t;

typedef struct wf_queue
{
	wf_segment_t* q;
	uint64_t tailQ;
	uint64_t headQ;
} wf_queue_t;

typedef struct wf_handle
{
	wf_segment_t* head;
	wf_segment_t* tail;
	volatile struct wf_handle* next;
	struct 
	{
		wf_enq_request_t req;
		volatile struct wf_handle* peer;
		uint64_t id;
	} enq;
	struct
	{
		wf_deq_request_t req;
		volatile struct wf_handle* peer;
		uint64_t id;
	} deq;
} wf_handle_t;


void printRes();

wf_segment_t* new_segment(uint64_t id);
cell_t* find_cell(wf_segment_t** sp, uint64_t cell_id);
void advance_end_for_linearizability(uint64_t* E, uint64_t cid);

/*
void init_wf_enq_request()
void init_wf_deq_request()
void init_wf_queue
void init_wf_cell

*/

void init_wf_queue(volatile wf_queue_t* q);
void init_wf_handle(volatile wf_handle_t* handle, wf_segment_t* init_seg);

uint64_t wf_queue_size(volatile wf_queue_t* q);
bool wf_queue_contain(volatile  wf_queue_t* q, void* val);

void enqueue(volatile wf_queue_t* q, volatile wf_handle_t* h, void* v);
bool try_to_claim_req(uint64_t* s, uint64_t id, uint64_t cell_id);
void enq_commit(volatile wf_queue_t* q, cell_t* c, void* v, uint64_t cid);
bool enq_fast(volatile wf_queue_t* q, volatile wf_handle_t* h, void* v, uint64_t* cid);
void enq_slow(volatile wf_queue_t* q, volatile wf_handle_t* h, void* v, uint64_t cell_id);
void* help_enq(volatile wf_queue_t* q, volatile wf_handle_t* h, cell_t* c, uint64_t i);

void* dequeue(volatile wf_queue_t* q, volatile wf_handle_t* h);
void* deq_fast(volatile wf_queue_t* q, volatile wf_handle_t* h, uint64_t* id);
void* deq_slow(volatile wf_queue_t* q, volatile wf_handle_t* h, uint64_t cid);
void help_deq(volatile wf_queue_t* q, volatile wf_handle_t* h, volatile wf_handle_t* helpee);


#endif //ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H

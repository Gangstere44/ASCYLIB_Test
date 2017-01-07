

#ifndef ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H
#define ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic_ops_if.h"
#include "utils.h"
#include "ssmem.h"
#include "common.h"

#define PATIENCE 10
#define MAX_GARBAGE(n) (10 * n)
#define SEG_LENGTH (1 << 10)

//#define CHECK_CORRECTNESS 1
/* record fast - slow path ratio */
//#define RECORD_F_S 1

// present in order to use test_simple.c 
extern __thread ssmem_allocator_t* alloc;


typedef struct wf_enq_request
{
	void* volatile val;
	struct
	{
		// id uses to differentiate req of a thread
		uint64_t volatile id 		: 63; 
		uint64_t volatile pending 	: 1;
	} state;
} wf_enq_request_t;

typedef struct wf_deq_request
{
	// id uses to differentiate req of a thread
	uint64_t volatile id;
	struct
	{
		// the cell idx for deq req
		uint64_t volatile idx 		: 63;
		uint64_t volatile pending 	: 1;
	} state;
} wf_deq_request_t;

typedef struct cell
{
	void* volatile  val;
	wf_enq_request_t* volatile enq;
	wf_deq_request_t* volatile deq;
} cell_t;

#define TAIL_CONST_VAL ((void*) 1)
#define HEAD_CONST_VAL ((void*) 2)
#define EMPTY ((void*) 0)
#define TAIL_ENQ_VAL   ((wf_enq_request_t*) 1)
#define HEAD_ENQ_VAL   ((wf_enq_request_t*) 2)
#define TAIL_DEQ_VAL   ((wf_deq_request_t*) 1)
#define HEAD_DEQ_VAL   ((wf_deq_request_t*) 2)


typedef struct wf_segment
{
	uint64_t id;
	struct wf_segment* volatile next;
	cell_t cells[SEG_LENGTH];
} wf_segment_t;

typedef struct wf_queue
{
	uint64_t num_thr;
	wf_segment_t* volatile q;
	volatile uint64_t tailQ;
	volatile uint64_t headQ;
	/* used for memory reclamation
	reference to the last seg id of the queue */
	volatile uint64_t I;

	#ifdef CHECK_CORRECTNESS
	uint64_t tot_sum_enq;
	uint64_t tot_sum_deq;
	#endif

	#ifdef RECORD_F_S
	uint64_t tot_fast_enq;
	uint64_t tot_slow_enq;
	uint64_t tot_fast_deq;
	uint64_t tot_slow_deq;
	#endif
} wf_queue_t;

typedef struct wf_handle
{
	wf_segment_t* volatile head;
	wf_segment_t* volatile tail;
	// handle are connected together in a ring
	struct wf_handle* next;
	// hazard pointer for memory reclamation
	wf_segment_t* volatile hzdp;
	uint64_t tid;

	struct 
	{	
		// my request
		wf_enq_request_t req;
		// handle that could need help
		struct wf_handle* volatile peer;
		// id req I m helping
		volatile uint64_t id;
	} enq;
	struct
	{
		wf_deq_request_t req;
		struct wf_handle* volatile peer;
		volatile uint64_t id;
	} deq;

	#ifdef CHECK_CORRECTNESS
	uint64_t sum_enq;
	uint64_t sum_deq;
	#endif

	#ifdef RECORD_F_S
	uint64_t fast_enq;
	uint64_t slow_enq;
	uint64_t fast_deq;
	uint64_t slow_deq;
	#endif
} wf_handle_t;


void printRes();

wf_queue_t* init_wf_queue(uint64_t num_thr);
void init_wf_handle(wf_handle_t* handle, wf_segment_t* init_seg, uint64_t tid);

wf_segment_t* wf_new_segment(uint64_t id);
cell_t* wf_find_cell(wf_segment_t* volatile * sp, uint64_t cell_id);
void wf_advance_end_for_linearizability(uint64_t volatile * E, uint64_t cid);

uint64_t wf_queue_size(wf_queue_t* q);
bool wf_queue_contain(wf_queue_t* q, void* val);

void wf_reclaim_records(wf_queue_t* q, wf_handle_t* h);
void wf_reclaim_correctness(wf_queue_t* q, wf_handle_t* h);
void wf_sum_queue(wf_queue_t* q);

void wf_enqueue(wf_queue_t* q, wf_handle_t* h, void* v);
bool wf_try_to_claim_req(uint64_t* s, uint64_t id, uint64_t cell_id);
void wf_enq_commit(wf_queue_t* q, cell_t* c, void* v, uint64_t cid);
bool wf_enq_fast(wf_queue_t* q, wf_handle_t* h, void* v, uint64_t* cid);
void wf_enq_slow(wf_queue_t* q, wf_handle_t* h, void* v, uint64_t cell_id);
void* wf_help_enq(wf_queue_t* q, wf_handle_t* h, cell_t* c, uint64_t i);

void* wf_dequeue(wf_queue_t* q, wf_handle_t* h);
void* wf_deq_fast(wf_queue_t* q, wf_handle_t* h, uint64_t* id);
void* wf_deq_slow(wf_queue_t* q, wf_handle_t* h, uint64_t cid);
void wf_help_deq(wf_queue_t* q, wf_handle_t* h, wf_handle_t* helpee);

void wf_cleanup(wf_queue_t* q, wf_handle_t* h);
void wf_update(wf_segment_t* volatile * from, wf_segment_t** to, wf_handle_t* h);
void wf_verify(wf_segment_t** seg, wf_segment_t* volatile hzdp);


#endif //ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H

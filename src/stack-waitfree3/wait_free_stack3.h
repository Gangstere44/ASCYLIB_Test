
#ifndef ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H
#define ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H


#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ssmem.h"
#include "atomic_ops_if.h"
#include "getticks.h"

#define W 6
#define EMPTY ((void*) 0)
#define MARKED ((void*) 1)
#define PATIENCE 10

extern __thread ssmem_allocator_t* alloc_wf;

typedef struct cell {

	void* value;

} cell_t;

typedef struct wf_segment
{
	uint64_t id;
	struct wf_segment* prev;
	void* cells[W];

} wf_segment_t;



typedef struct handle {

	int64_t ttd; // tid_to_help

} handle_t;

typedef struct wf_stack {

	uint64_t num_thr;

	uint64_t top_id;
	wf_segment_t* top;

	handle_t* handles;

	volatile int64_t clean_tid;

} wf_stack_t;

wf_stack_t* init_wf_stack(uint64_t num_thr);
wf_segment_t* wf_new_segment(uint64_t id, wf_segment_t* prev);

cell_t* wf_find_cell(wf_stack_t* s, uint64_t cell_id);
uint64_t stack_size(wf_stack_t* s);

void push(wf_stack_t* s, int64_t tid, void* value);

void* pop(wf_stack_t* s, int64_t tid);

void try_clean_up(wf_stack_t* s, int64_t tid);

#endif
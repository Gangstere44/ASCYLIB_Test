
#ifndef STACK_WAITFREE_SEG_H
#define STACK_WAITFREE_SEG_H

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ssmem.h"
#include "atomic_ops_if.h"
#include "common.h"

#define W 6
#define EMPTY ((void*) 0)
#define MARKED ((void*) 1)

extern __thread ssmem_allocator_t* alloc_wf;

typedef struct cell {

	void* volatile value;

} cell_t;

typedef struct wf_segment
{
	int64_t id;
	struct wf_segment* volatile prev;
	/* W taken to fit the struct inside
	one cache block of 64 bytes (here W = 6) */
	void* cells[W];

} wf_segment_t;

typedef struct wf_stack {

	uint64_t num_thr;

	wf_segment_t sentinel;
	volatile int64_t top_id;
	wf_segment_t* volatile top;

	volatile int64_t clean_tid;

} wf_stack_t;

wf_stack_t* init_wf_stack(uint64_t num_thr);
wf_segment_t* wf_new_segment(int64_t id, wf_segment_t* prev);

uint64_t stack_size(wf_stack_t* s);

void add_segment(wf_stack_t* s, int64_t seg_id, wf_segment_t* prev);
cell_t* find_cell(wf_stack_t* s, uint64_t cell_id);

void push(wf_stack_t* s, int64_t tid, void* value);

void* pop(wf_stack_t* s, int64_t tid);

void try_clean_up(wf_stack_t* s, int64_t tid);

#endif /* STACK_WAITFREE_SEG_H */
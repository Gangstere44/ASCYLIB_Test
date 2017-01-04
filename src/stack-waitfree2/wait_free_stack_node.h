

#ifndef ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H
#define ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H


#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ssmem.h"
#include "atomic_ops_if.h"
#include "getticks.h"

#define W 1
#define MARK_FOR_DEL ((void*) 1)
#define EMPTY_STACK 0

/* this values should be computed
   at run time */
#define MAX_PUSH_PATIENCE 5
#define MIN_PUSH_PATIENCE 1

#define MAX_POP_PATIENCE 10
#define MIN_POP_PATIENCE 1 

#define MAX_NODE_TO_FREE 100
#define MIN_NODE_TO_FREE 5

extern __thread ssmem_allocator_t* alloc_wf;

typedef struct node {
	
	void* value;
	struct node* volatile next_done;
	struct node* volatile prev;
	volatile bool mark;
	int64_t push_tid;
	uint64_t index;
	
	// size 2 to get a total size of 64 bytes
	uint64_t padding[2];

} node_t;

typedef struct push_op {
	
	volatile bool pushed;
	node_t* node;
	
	// size 6 to get a total size of 64 bytes
	uint64_t padding[6];
	
} push_op_t;

typedef struct handle {
	
	int64_t ttd; // tid_to_help
	
	/* every thread as its own patience
	as some could work slower than other */
	uint64_t push_patience;
	uint64_t pop_patience;
	
} handle_t;

typedef struct wf_stack {
	
	uint64_t num_thr;
	node_t sentinel;
	node_t* volatile top;
	
	handle_t* handles;  
	push_op_t* volatile * announces; 
	
	volatile int64_t clean_tid;
	uint64_t node_to_free;
	
} wf_stack_t;

wf_stack_t* init_wf_stack(uint64_t num_thr);
node_t* init_node(void* value, int64_t push_tid);
push_op_t* init_push_op(node_t* n);

int64_t tid_to_help(wf_stack_t* s, int64_t tid);
uint64_t stack_size(wf_stack_t* s);

void push(wf_stack_t* s, int64_t tid, void* value);
bool push_fast(wf_stack_t* s, node_t* n);
void post_request(wf_stack_t* s, node_t* n, int64_t tid);
void push_slow(wf_stack_t* s, int64_t tid);

void* pop(wf_stack_t* s, int64_t tid);
bool fast_pop(wf_stack_t* s, int64_t tid, node_t** ret_n);
void slow_pop(wf_stack_t* s, int64_t tid, node_t** ret_n);

void try_clean_up(wf_stack_t* s, int64_t tid);
void clean(node_t* left, node_t* right);

#endif

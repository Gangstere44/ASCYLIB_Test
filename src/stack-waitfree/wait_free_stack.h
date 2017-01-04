

#ifndef ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H
#define ASCYLIB_PROJECT_WAIT_FREE_QUEUE_H


#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ssmem.h"
#include "atomic_ops_if.h"
#include "getticks.h"

// size of a segment to free
#define W 2 
/* unique value that you can't
   push into the stack */
#define EMPTY_STACK 0 

extern __thread ssmem_allocator_t* alloc_wf;

typedef struct node {
	
	void* value;
	struct node* volatile next_done;
	struct node* volatile prev;
	volatile bool mark;
	int64_t push_tid;
	volatile uint64_t index;
	volatile uint64_t counter;
	
	/* size of 1 to reach 64 bytes */
	uint64_t padding[1];

} node_t;

typedef struct push_op {
	
	uint64_t phase;
	volatile bool pushed;
	node_t* node;
	
	/* size of 5 to reach 64 bytes */
	uint64_t padding[5];
	
} push_op_t;

typedef struct delete_req {
	
	uint64_t phase;
	int64_t tid;
	volatile bool pending;
	node_t* node;
	
	/* size of 4 to reach 64 bytes */
	uint64_t padding[4];

} delete_req_t;

typedef struct wf_stack {
	
	uint64_t num_thr;
	node_t sentinel;
	node_t* volatile top;
	push_op_t* volatile * announces; 
	delete_req_t* volatile * all_delete_requests;
	volatile uint64_t phase_counter_push_req;
	volatile uint64_t phase_counter_del_req;
	volatile delete_req_t* volatile unique_req;
	
} wf_stack_t;



wf_stack_t* init_wf_stack(uint64_t num_thr);
node_t* init_node(void* value, int64_t push_tid);
push_op_t* init_push_op(uint64_t phase, node_t* n);
delete_req_t* init_delete_req(uint64_t phase, int64_t tid, node_t* n);

uint64_t stack_size(wf_stack_t* s);

void push(wf_stack_t* s, int64_t tid, void* value);
void help(wf_stack_t* s,push_op_t* request, int64_t tid);
void attach_node(wf_stack_t* s,push_op_t* request, int64_t tid);
void update_top(wf_stack_t* s, int64_t tid);

node_t* pop(wf_stack_t* s, int64_t tid);
void try_clean_up(wf_stack_t* s, node_t* n, int64_t tid, bool from_right_node);
void help_finish_delete(wf_stack_t* s, int64_t tid);

void clean(wf_stack_t* s, int64_t tid, node_t* n);
void help_delete(wf_stack_t* s, delete_req_t* dr, int64_t tid);
void unique_delete(wf_stack_t* s, delete_req_t* dr, int64_t tid);

#endif
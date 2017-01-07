
#include "queue-timestamp.h"

__thread ssmem_allocator_t* alloc_ts;

/*  */
uint64_t rdtscp(void) {
	uint32_t lo, hi;
	__asm__ volatile ("rdtscp"
		: /* outputs */ "=a" (lo), "=d" (hi)
		: /* no input*/
		: /* clobbers */ "%rcx" );
	return (uint64_t) lo | (((uint64_t) hi) << 32);
}

ts_queue_t* ts_new_queue(int64_t num_thread) {

	ts_queue_t* new_q = malloc(sizeof(ts_queue_t));

	new_q->num_thread = num_thread;
	new_q->counter_ts = 0;

	new_q->pools = calloc(num_thread, sizeof(pool_t*));
	uint64_t i;
	for(i = 0; i < num_thread; i++) {
		new_q->pools[i] = ts_new_pool(i);
	}

	// Strategy and delays by default
	new_q->delay_ticks = DELAY_TIME_TICKS;
	new_q->strategy = TS_NAIVE;
	
	return new_q;
}

pool_t* ts_new_pool(int64_t tid) {
	
	pool_t* new_pool = malloc(sizeof(pool_t));
	new_pool->head = NULL;
	new_pool->tail = NULL;
	
	return new_pool;
}

node_t* ts_new_node(void* val, bool taken) {
	
	node_t* new_node = ssmem_alloc(alloc_ts, sizeof(node_t));
	
	new_node->ts.begin = MAX_TIME_STAMP;
	new_node->ts.end = MAX_TIME_STAMP;
	new_node->ts.counter_value = MAX_TIME_STAMP;

	new_node->value = val;
	new_node->taken = taken;
	new_node->next = NULL;
	
	return new_node;
}

uint64_t ts_queue_size(ts_queue_t* q) {
	
	uint64_t size = 0;
	
	uint64_t i = 0;
	for(i = 0; i < q->num_thread; i++) {
		
		/* need to check over each thread */
		pool_t* p = q->pools[i];
		
		node_t* tmp = p->head;
		
		while(tmp != NULL) {
			
			if(!tmp->taken) {
				size++;
			}
			
			tmp = tmp->next;	
		}
	}

	return size;
}

void ts_enqueue(ts_queue_t* q, void* val, int64_t tid) {
	
	pool_t* pool = q->pools[tid];

	// insert the node physically in the thread queue
	node_t* n = ts_insert(q, pool, val);

	/* give a time stamp to the node following
	the strategy being in use */
	if(q->strategy == TS_NAIVE) {
		// get a simple integer timestamp
		n->ts.counter_value = FAI_U64(&q->counter_ts);
	} else if(q->strategy == TS_CAS) {
		// get an interval timestamp, optimize with CAS
		ts_CAS(q, &n->ts);
	} else if(q->strategy == TS_INTERVAL) {
		// get an interval timestamp 
		ts_interval(q, &n->ts);
	}
}

void ts_interval(ts_queue_t* q, time_stamp_t* ts) {

	ts->begin = rdtscp();

	cdelay(q->delay_ticks);

	ts->end = rdtscp();
}

void ts_CAS(ts_queue_t* q, time_stamp_t* ts) {

	ts->begin = q->counter_ts;
	
	cdelay(q->delay_ticks);

	ts->end = q->counter_ts;

	if(ts->begin != ts->end) {
		return;
	}

	if(CAS_U64_bool(&q->counter_ts, ts->begin, ts->begin + 1)) {
		return;
	}

	ts->end = q->counter_ts - 1;
}

void* ts_dequeue(ts_queue_t* q, int64_t tid) {
	
	time_stamp_t time_stamp = {.begin = MAX_TIME_STAMP, 
							   .end = MAX_TIME_STAMP, 
							   .counter_value = MAX_TIME_STAMP};

	/* give a time stamp to the dequeuer following
	the strategy being in use */						   
	if(q->strategy == TS_NAIVE) {
   		time_stamp.counter_value = FAI_U64(&q->counter_ts);
	} else if(q->strategy == TS_CAS) {
		ts_CAS(q, &time_stamp);
	} else if(q->strategy == TS_INTERVAL) {
		ts_interval(q, &time_stamp);
	}
	
	dequeue_request_t deq_req = {.success = false, .element = NULL};

	do {
		ts_try_remove(q, &time_stamp, &deq_req);
	} while(!deq_req.success);

	return deq_req.element;
}

void ts_try_remove(ts_queue_t* q, time_stamp_t* start_time, dequeue_request_t* deq_req) {

	node_t* new_head = NULL;
	
	time_stamp_t time_stamp = {.begin = MAX_TIME_STAMP, .end = MAX_TIME_STAMP, .counter_value = MAX_TIME_STAMP};
	
	pool_t* pool = NULL;
	node_t* head = NULL;
	node_t** empty = calloc(q->num_thread, sizeof(node_t*));
	
	uint64_t i;
	for(i = 0; i < q->num_thread; i++) {
		
		// we go over each pool
		pool_t* current = q->pools[i];

		// take the oldest req of this pool
		get_oldest_request_t old_req = {.node = NULL, .pool_head = NULL};
		ts_get_oldest(q, current, &old_req);
		
		// if it's empty, save for later
		if(old_req.node == NULL) {
			empty[i] = old_req.pool_head;
			continue;
		}
		
		// take the time stamp of the oldest req
		time_stamp_t node_time_stamp = old_req.node->ts;
		
		/* now we need to check if the oldest found req 
		can be popped by us => has a smaller time stamp than us */
		bool stat = false;
		// depends of the strategy
		if(q->strategy == TS_NAIVE) {
			stat = time_stamp.counter_value == MAX_TIME_STAMP 
					|| time_stamp.counter_value < node_time_stamp.counter_value;
		} else if(q->strategy == TS_CAS || q->strategy == TS_INTERVAL) {
			stat = time_stamp.begin == MAX_TIME_STAMP
					|| time_stamp.end < node_time_stamp.begin;
		}

		// if the node can be popped by us 
		if(stat) {	
			new_head = old_req.node;

			time_stamp.begin = node_time_stamp.begin;
			time_stamp.end = node_time_stamp.end;
			time_stamp.counter_value = node_time_stamp.counter_value;

			pool = current;
			head = old_req.pool_head;
		}
	}
	
	/* if no possible node was found we need to check
	that we weren't too fast => maybe some possible nodes
	were being pushed */
	if(new_head == NULL) {
		for(i = 0; i < q->num_thread; i++) {
			pool_t* current = q->pools[i];
			if(current->head != empty[i]) {
				return;
			}
		}
		
		/* if no new node appeared, the queue is empty
		 for us or no timestamp were small enough to be 
		 popped by us */
		deq_req->success = true;
		deq_req->element = EMPTY;
		return;
	}
	
	/* we popped a node, need to remove it */
	ts_remove(q, pool, head, new_head, deq_req);
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
	/* get the oldest unmarked node for the pool */
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

void ts_remove(ts_queue_t* q, pool_t* pool, node_t* old_head, node_t* new_head, dequeue_request_t* deq_req) {
	
	if(CAS_U64_bool(&new_head->taken, false, true)) {

		/* if the head isn't changed by somebody else
		=> we try to free node between the old head and 
		the one we dequeued, not include */
		if(CAS_U64_bool(&pool->head, old_head, new_head)) {
			
			node_t* tmp = old_head;
			node_t* next_tmp = tmp;
			/* we stop whenever a node isn't taken
			or the tmp is the node we are dequeuing */
			while(tmp->taken && tmp != new_head) {

				next_tmp = tmp->next;
				ssmem_free(alloc_ts, (void*) tmp);
				tmp = next_tmp;
			}

		}

		deq_req->success = true;
		deq_req->element = new_head->value;
		return;
	}

	// somebody was faster than us
	deq_req->success = false;
	deq_req->element = NULL;
}
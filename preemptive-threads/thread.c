#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

enum
{
	READY = 0,
	RUNNING = 1,
	EXITED = 2,
	BLOCKED = 3
};

/* This is the thread control block */
typedef struct thread
{
	Tid tid;
	struct thread *next;
	ucontext_t *mycontext;
	int status;
	void *sp;
	struct wait_queue *wait_queue;
} tcb_t;

typedef struct
{
	tcb_t *head;
	tcb_t *tail;
	int size;
} queue_t;

/* This is the wait queue structure */
struct wait_queue
{
	queue_t *queue;
};

void dequeue(queue_t *queue, tcb_t *thread);
void enqueue(queue_t *queue, tcb_t *thread);
void free_queue(queue_t *queue);
tcb_t *find_thr_by_id(Tid tid, queue_t *queue);

tcb_t *running_thread;				   //the thread is RUNNING
queue_t *ready_queue;				   //threads that have status READY
queue_t *exit_queue;				   //threads that have status EXITED
tcb_t *available_thr[THREAD_MAX_THREADS]; //available array, index is id, 0 is available, 1 is taken
int exit_code_array[THREAD_MAX_THREADS]; //exit code array

void thread_init(void)
{
	//initializing first thread
	running_thread = malloc(sizeof(tcb_t));
	running_thread->tid = 0;
	running_thread->mycontext = malloc(sizeof(ucontext_t));
	running_thread->status = RUNNING;
	running_thread->wait_queue = wait_queue_create();
	int err = getcontext(running_thread->mycontext);
	assert(!err);
	//initializing avlb array
	available_thr[0] = running_thread;
	for (int i = 1; i < THREAD_MAX_THREADS; i++)
	{
		available_thr[i] = NULL;
	}
	//initializing queue
	ready_queue = malloc(sizeof(queue_t));
	ready_queue->head = NULL;
	ready_queue->tail = NULL;
	ready_queue->size = 0;
	exit_queue = malloc(sizeof(queue_t));
	exit_queue->head = NULL;
	exit_queue->tail = NULL;
	exit_queue->size = 0;
}

Tid thread_id()
{
	int enabled = interrupts_off();
	if (running_thread != NULL)
	{
		interrupts_set(enabled);
		return running_thread->tid;
	}
	interrupts_set(enabled);
	return THREAD_INVALID;
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void thread_stub(void (*thread_main)(void *), void *arg)
{
	
	interrupts_on();
	thread_main(arg); // call thread_main() function with arg
	thread_exit(0);
}

Tid thread_create(void (*fn)(void *), void *parg)
{
	int enabled = interrupts_off();
	tcb_t *newthr = malloc(sizeof(tcb_t));
	ucontext_t *newcon = malloc(sizeof(ucontext_t));
	void *sp = malloc(THREAD_MIN_STACK + 16);
	if (newthr == NULL || sp == NULL || newcon == NULL)
	{
		free(newcon);
		free(sp);
		free(newthr);
		interrupts_set(enabled);
		return THREAD_NOMEMORY;
	}

	for (int j = 0; j < THREAD_MAX_THREADS; j++)
	{
		if (available_thr[j] == NULL)
		{
			newthr->tid = j;
			available_thr[j] = newthr;
			break;
		}
		if (j == THREAD_MAX_THREADS - 1 && available_thr[j] != NULL)
		{
			interrupts_set(enabled);
			return THREAD_NOMORE;
		}
	}

	newthr->wait_queue = wait_queue_create();
	newthr->status = READY;
	newthr->sp = sp;
	newthr->mycontext = newcon;
	newthr->next = NULL;

	int err = getcontext(newthr->mycontext);
	assert(!err);
	void *sp_top = sp + THREAD_MIN_STACK - 8;
	sp_top -= (unsigned long)sp_top % 16;
	sp_top += 8;
	newthr->mycontext->uc_mcontext.gregs[REG_RSP] = (unsigned long)sp_top;
	newthr->mycontext->uc_mcontext.gregs[REG_RIP] = (unsigned long)thread_stub;
	newthr->mycontext->uc_mcontext.gregs[REG_RDI] = (unsigned long)fn;
	newthr->mycontext->uc_mcontext.gregs[REG_RSI] = (unsigned long)parg;
	enqueue(ready_queue, newthr);
	interrupts_set(enabled);

	return newthr->tid;
}

void free_queue(queue_t *queue)
{
	tcb_t *curr = queue->head;
	while (curr != NULL)
	{
		dequeue(queue, curr);
		wait_queue_destroy(curr->wait_queue);
		free(curr->sp);
		free(curr->mycontext);
		free(curr);
		curr = queue->head;
	}
}

Tid thread_yield(Tid want_tid)
{
	int enabled = interrupts_off();
	free_queue(exit_queue);
	Tid ret;
	if (running_thread->status == EXITED)
	{
		thread_exit(0);
	}

	if (want_tid < 0)
	{
		if (want_tid == THREAD_ANY && ready_queue->size == 0)
		{
			interrupts_set(enabled);
			return THREAD_NONE;
		}
		if (want_tid == THREAD_SELF)
		{
			interrupts_set(enabled);
			return running_thread->tid;
		}
	}
	if (want_tid >= -1 && want_tid < THREAD_MAX_THREADS)
	{
		tcb_t *ready = NULL;
		if (want_tid == THREAD_ANY)
		{
			ready = ready_queue->head;
		}
		else
		{ //given a specific tid
			if (want_tid == running_thread->tid)
			{
				interrupts_set(enabled);
				return running_thread->tid;
			}
			if (available_thr[want_tid] == NULL)
			{
				interrupts_set(enabled);
				return THREAD_INVALID;
			}
			ready = available_thr[want_tid];
		}

		//storing caller
		volatile int setcon_flag = 0;
		int err = getcontext(running_thread->mycontext);
		assert(!err);

		if (setcon_flag == 0)
		{
			setcon_flag = 1;
			dequeue(ready_queue, ready);
			enqueue(ready_queue, running_thread);
			running_thread->status = READY;
			running_thread = ready;
			if(running_thread->status != EXITED){
				ready->status = RUNNING;
			}
			ret = running_thread->tid;
			setcontext(running_thread->mycontext);
		}
		free_queue(exit_queue);
		setcon_flag = 0;
		interrupts_set(enabled);
		return ret;
	}
	interrupts_set(enabled);
	return THREAD_INVALID;
}

void thread_exit(int exit_code)
{
	int enabled = interrupts_off();
	if (ready_queue->size == 0 && running_thread->wait_queue->queue->size == 0)
	{
		wait_queue_destroy(running_thread->wait_queue);
		free(running_thread->sp);
		free(running_thread->mycontext);
		free(running_thread);
		free_queue(ready_queue);
		free(ready_queue);
		free_queue(exit_queue);
		free(exit_queue);
		interrupts_set(enabled);
		exit(exit_code);
	}

	thread_wakeup(running_thread->wait_queue, 1);
	running_thread->status = EXITED;
	exit_code_array[running_thread->tid] = exit_code;
	available_thr[running_thread->tid] = NULL;
	enqueue(exit_queue, running_thread);

	volatile int setcon_flag = 0;
	assert(!getcontext(running_thread->mycontext));
	
	if(setcon_flag == 0){
		setcon_flag = 1;
		if(ready_queue->head->status != EXITED){
			ready_queue->head->status = RUNNING;
		}
		running_thread = ready_queue->head;
		dequeue(ready_queue, ready_queue->head);
		setcontext(running_thread->mycontext);
	}else{
		free_queue(exit_queue);
		interrupts_set(enabled);
		exit(exit_code);
	}
	

}

Tid thread_kill(Tid tid)
{
	int enabled = interrupts_off();
	if (tid < 0 || tid >= THREAD_MAX_THREADS || available_thr[tid] == NULL || tid == running_thread->tid)
	{
		interrupts_set(enabled);
		return THREAD_INVALID;
	}

	tcb_t *curr = available_thr[tid];
	curr->status = EXITED;
	interrupts_set(enabled);
	return curr->tid;
}


tcb_t *find_thr_by_id(Tid tid, queue_t *queue){
	tcb_t *curr = queue->head;
	while(curr != NULL){
		if(curr->tid == tid){
			return curr;
		}
		curr = curr->next;
	}
	return NULL;
}

void enqueue(queue_t *queue, tcb_t *thread)
{
	if (queue->size == 0)
	{
		queue->head = thread;
		queue->tail = thread;
	}
	else
	{
		queue->tail->next = thread;
		queue->tail = thread;
	}
	queue->size++;
}

void dequeue(queue_t *queue, tcb_t *thread)
{
	if (queue->size == 1)
	{
		queue->head = NULL;
		queue->tail = NULL;
	}
	else if (queue->size == 2)
	{
		if (queue->head == thread)
		{
			queue->head = thread->next;
			queue->tail = thread->next;
		}
		else
		{
			queue->head = thread;
			queue->tail = queue->head;
		}
	}
	else
	{
		tcb_t *curr = queue->head;
		tcb_t *prev = NULL;
		while (curr != NULL)
		{
			if (curr->tid == thread->tid)
			{
				if (curr == queue->head)
				{
					queue->head = thread->next;
					break;
				}
				else if (curr == queue->tail)
				{
					queue->tail = prev;
					prev->next = NULL;
					break;
				}
				else
				{
					prev->next = curr->next;
					break;
				}
			}
			else
			{
				prev = curr;
				curr = curr->next;
			}
		}
	}
	thread->next = NULL;
	queue->size--;
}

/**************************************************************************
 * Important: The rest of the code should be implemented in Assignment 2. *
 **************************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	int enabled = interrupts_off();
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);
	wq->queue = malloc(sizeof(queue_t));
	wq->queue->head = NULL;
	wq->queue->tail = NULL;
	wq->queue->size = 0;
	interrupts_set(enabled);
	return wq;
}

void wait_queue_destroy(struct wait_queue *wq)
{
	int enabled = interrupts_off();
	assert(wq->queue->size == 0);
	free_queue(wq->queue);
	free(wq->queue);
	free(wq);
	interrupts_set(enabled);
}

Tid thread_sleep(struct wait_queue *queue)
{
	int enabled = interrupts_off();

	if(queue == NULL){
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	if(ready_queue->head == NULL){
		interrupts_set(enabled);
		return THREAD_NONE;
	}

	volatile int setcon_flag = 0;
	assert(!getcontext(running_thread->mycontext));

	if(setcon_flag == 0){
		setcon_flag = 1;
		running_thread->status = BLOCKED;
		enqueue(queue->queue, running_thread);
		ready_queue->head->status = RUNNING;
		running_thread = ready_queue->head;
		dequeue(ready_queue, ready_queue->head);
		setcontext(running_thread->mycontext);
	}else{
		setcon_flag = 0;
		interrupts_set(enabled);
		return running_thread->tid;
	}

	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int thread_wakeup(struct wait_queue *queue, int all)
{
	int enabled = interrupts_off();
	if(queue == NULL || queue->queue == NULL){
		interrupts_set(enabled);
		return 0;
	}

	int count = 0;
	tcb_t *curr = queue->queue->head;
	while(curr != NULL){
		tcb_t *temp = curr;
		curr = curr->next;
		dequeue(queue->queue, temp);
		enqueue(ready_queue, temp);
		if(temp->status != EXITED){
			temp->status = READY;
		}
		if(all == 0){
			interrupts_set(enabled);
			return 1;
		}
		count ++;
	}
	interrupts_set(enabled);
	return count;
}

/* suspend current thread until Thread tid exits */
Tid thread_wait(Tid tid, int *exit_code)
{
	int enabled = interrupts_off();

	if(tid == running_thread->tid || tid < 0 || tid >= THREAD_MAX_THREADS || available_thr[tid] == NULL){
		interrupts_set(enabled);		
		return THREAD_INVALID;
	}

	Tid ttid;
	if(available_thr[tid]->status != EXITED){
		ttid = thread_sleep(available_thr[tid]->wait_queue);
	}

	//resumes after target thread exited
	if(exit_code){
		*exit_code = exit_code_array[tid];
	}

	if(ttid == THREAD_NONE || ttid == THREAD_INVALID){
		interrupts_set(enabled);
		return THREAD_INVALID;
	}

	interrupts_set(enabled);
	return tid;
}

struct lock
{
	int state;
	struct wait_queue *wait_queue;
};

struct lock *
lock_create()
{
	int enabled = interrupts_off();
	struct lock *lock;
	lock = malloc(sizeof(struct lock));
	assert(lock);
	lock->wait_queue = wait_queue_create();
	lock->state = 0;
	interrupts_set(enabled);
	return lock;
}

void lock_destroy(struct lock *lock)
{
	assert(lock != NULL);
	int enabled = interrupts_off();
	if(lock->state == 0){
		wait_queue_destroy(lock->wait_queue);
	}
	free(lock);
	interrupts_set(enabled);
}

int test_set(struct lock *lock){
	int old = lock->state;
	lock->state = 1;
	return old;
}

void lock_acquire(struct lock *lock)
{
	int enabled = interrupts_off();
	assert(lock != NULL);
	while(test_set(lock)){
		thread_sleep(lock->wait_queue);
	}
	interrupts_set(enabled);
}

void lock_release(struct lock *lock)
{
	int enabled = interrupts_off();
	assert(lock != NULL);
	if(lock->state == 1){
		thread_wakeup(lock->wait_queue, 1);
		lock->state = 0;
	}
	interrupts_set(enabled);
}

struct cv
{
	struct wait_queue *wait_queue;
};

struct cv *
cv_create()
{
	int enabled = interrupts_off();
	struct cv *cv;
	cv = malloc(sizeof(struct cv));
	assert(cv);
	cv->wait_queue = wait_queue_create();
	interrupts_set(enabled);
	return cv;
}

void cv_destroy(struct cv *cv)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	wait_queue_destroy(cv->wait_queue);
	free(cv);
	interrupts_set(enabled);
}

void cv_wait(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	if(lock->state == 1){
		lock_release(lock);
		thread_sleep(cv->wait_queue);
		lock_acquire(lock);
	}
	interrupts_set(enabled);
}

void cv_signal(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	if(lock->state == 1){
		thread_wakeup(cv->wait_queue, 0);
	}
	interrupts_set(enabled);
}

void cv_broadcast(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	if(lock->state == 1){
		thread_wakeup(cv->wait_queue, 1);
	}
	interrupts_set(enabled);
}
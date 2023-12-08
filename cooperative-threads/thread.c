#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

enum
{
	READY = 0,
	RUNNING = 1,
	EXITED = 2
};

/* This is the wait queue structure */
struct wait_queue
{
	/* ... Fill this in Assignment 2 ... */
};

/* This is the thread control block */
typedef struct thread
{
	Tid tid;
	struct thread *next;
	ucontext_t *mycontext;
	int status;
	void *sp;
} tcb_t;

typedef struct
{
	tcb_t *head;
	tcb_t *tail;
	int size;
} queue_t;

void dequeue(queue_t *queue, tcb_t *thread);
void enqueue(queue_t *queue, tcb_t *thread);
void free_queue();

tcb_t *running_thread;				   //the thread is RUNNING
queue_t *ready_queue;				   //threads that have status READY
queue_t *exit_queue;				   //threads that have status EXITED
int available_thr[THREAD_MAX_THREADS]; //available id array, index is id, 0 is available, 1 is taken

void thread_init(void)
{
	//initializing first thread
	running_thread = malloc(sizeof(tcb_t));
	running_thread->tid = 0;
	running_thread->mycontext = malloc(sizeof(ucontext_t));
	running_thread->status = RUNNING;
	int err = getcontext(running_thread->mycontext);
	assert(!err);
	//initializing avlb array
	available_thr[0] = 1;
	for (int i = 1; i < THREAD_MAX_THREADS; i++)
	{
		available_thr[i] = 0;
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
	return;
}

Tid thread_id()
{
	if (running_thread != NULL)
	{
		return running_thread->tid;
	}
	return THREAD_INVALID;
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void thread_stub(void (*thread_main)(void *), void *arg)
{
	thread_main(arg); // call thread_main() function with arg
	thread_exit(0);
}

Tid thread_create(void (*fn)(void *), void *parg)
{
	tcb_t *newthr = malloc(sizeof(tcb_t));
	ucontext_t *newcon = malloc(sizeof(ucontext_t));
	void *sp = malloc(THREAD_MIN_STACK + 16);
	if (newthr == NULL || sp == NULL || newcon == NULL)
	{
		free(newcon);
		free(sp);
		free(newthr);
		return THREAD_NOMEMORY;
	}

	for (int j = 0; j < THREAD_MAX_THREADS; j++)
	{
		if (available_thr[j] == 0)
		{
			newthr->tid = j;
			available_thr[j] = 1;
			break;
		}
		if (j == THREAD_MAX_THREADS - 1 && available_thr[j] == 1)
		{
			return THREAD_NOMORE;
		}
	}

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

	return newthr->tid;
}

void free_queue(queue_t * queue)
{
	tcb_t *curr = queue->head;
	while (curr != NULL)
	{
		dequeue(queue, curr);
		free(curr->sp);
		free(curr->mycontext);
		free(curr);
		curr = queue->head;
	}
}

Tid thread_yield(Tid want_tid)
{
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
			return THREAD_NONE;
		}
		if (want_tid == THREAD_SELF)
		{
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
				return running_thread->tid;
			}
			if (available_thr[want_tid] == 0)
			{
				return THREAD_INVALID;
			}
			tcb_t *curr = ready_queue->head;
			while (curr != NULL)
			{
				if (curr->tid == want_tid)
				{
					ready = curr;
					break;
				}
				else
				{
					curr = curr->next;
				}
			}
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
		return ret;
	}
	return THREAD_INVALID;
}

void thread_exit(int exit_code)
{
	if (ready_queue->size == 0)
	{
		free(running_thread->sp);
		free(running_thread->mycontext);
		free(running_thread);
		free(ready_queue);
		free_queue(exit_queue);
		free(exit_queue);
		exit(exit_code);
	}

	running_thread->status = EXITED;
	available_thr[running_thread->tid] = 0;
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
		free(running_thread->sp);
		free(running_thread->mycontext);
		free(running_thread);
		exit(exit_code);
	}
	

}

Tid thread_kill(Tid tid)
{
	if (!(tid >= 0 && tid < THREAD_MAX_THREADS && available_thr[tid] == 1 && tid != running_thread->tid))
	{
		return THREAD_INVALID;
	}
	tcb_t *curr = ready_queue->head;
	while (curr != NULL)
	{
		if (curr->tid == tid)
		{
			curr->status = EXITED;
			return curr->tid;
		}
		else
		{
			curr = curr->next;
		}
	}
	return THREAD_INVALID;
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
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	TBD();

	return wq;
}

void wait_queue_destroy(struct wait_queue *wq)
{
	TBD();
	free(wq);
}

Tid thread_sleep(struct wait_queue *queue)
{
	TBD();
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int thread_wakeup(struct wait_queue *queue, int all)
{
	TBD();
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid thread_wait(Tid tid, int *exit_code)
{
	TBD();
	return 0;
}

struct lock
{
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv
{
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

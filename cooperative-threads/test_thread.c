#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>
#include <malloc.h>
#include "thread.h"
#include "test_thread.h"

#define DURATION  60000000
#define NTHREADS       128
#define LOOPS	        10

static void grand_finale();
static void hello(char *msg);
static int fact(int n);
static void suicide();
static void finale();
static int set_flag(int val);

long *stack_array[THREAD_MAX_THREADS];

static void
mallinfo_check(struct mallinfo *start)
{
	struct mallinfo end = mallinfo();
	/* mallinfo is not entirely reliable. 
	 * we are lax with checking memory leaks */
	assert(end.uordblks <= (start->uordblks  + (2 * THREAD_MIN_STACK)));
	assert(end.hblks == start->hblks);
}

/* Important: these tests assume that preemptive scheduling is not enabled,
 * i.e., register_interrupt_handler is NOT called before this function is
 * called. */
void
test_basic()
{
	Tid ret;
	Tid ret2;
	int allocated_space;

	printf("starting basic test\n");
	struct mallinfo minfo_start = mallinfo();

	assert(thread_id() == 0);

	/* Initial thread yields */
	ret = thread_yield(THREAD_SELF);
	assert(thread_ret_ok(ret));
	printf("initial thread returns from yield(SELF)\n");
	/* See thread.h -- initial thread must be Tid 0 */
	ret = thread_yield(0);
	assert(thread_ret_ok(ret));
	printf("initial thread returns from yield(0)\n");
	ret = thread_yield(THREAD_ANY);
	assert(ret == THREAD_NONE);
	printf("initial thread returns from yield(ANY)\n");
	ret = thread_yield(0xDEADBEEF);
	assert(ret == THREAD_INVALID);
	printf("initial thread returns from yield(INVALID)\n");
	ret = thread_yield(16);
	assert(ret == THREAD_INVALID);
	printf("initial thread returns from yield(INVALID2)\n");

	struct mallinfo minfo;
	minfo = mallinfo();
	allocated_space = minfo.uordblks;
	/* create a thread */
	ret = thread_create((void (*)(void *))hello, "hello from first thread");
	minfo = mallinfo();
	if (minfo.uordblks <= allocated_space) {
		printf("it appears that the thread stack is not being"
		       "allocated dynamically\n");
		assert(0);
	}
	printf("my id is %d\n", thread_id());
	assert(thread_ret_ok(ret));
	ret2 = thread_yield(ret);
	assert(ret2 == ret);

	/* store address of some variable on stack */
	stack_array[thread_id()] = (long *)&ret;

	int ii, jj;
	/* we will be using THREAD_MAX_THREADS threads later */
	Tid child[THREAD_MAX_THREADS];
	char msg[NTHREADS][1024];
	/* create NTHREADS threads */
	for (ii = 0; ii < NTHREADS; ii++) {
		ret = snprintf(msg[ii], 1023, "hello from thread %3d", ii);
		assert(ret > 0);
		child[ii] = thread_create((void (*)(void *))hello, msg[ii]);
		assert(thread_ret_ok(child[ii]));
	}
	printf("my id is %d\n", thread_id());
	for (ii = 0; ii < NTHREADS; ii++) {
		ret = thread_yield(child[ii]);
		assert(ret == child[ii]);
	}

	/* destroy NTHREADS + 1 threads we just created */
	printf("destroying all threads\n");
	ret = thread_kill(ret2);
	assert(ret == ret2);
	for (ii = 0; ii < NTHREADS; ii++) {
		ret = thread_kill(child[ii]);
		assert(ret == child[ii]);
	}

	/* we destroyed other threads. yield so that these threads get to run
	 * and exit. */
	ii = 0;
	do {
		/* the yield should be needed at most NTHREADS+2 times */
		assert(ii <= (NTHREADS + 1));
		ret = thread_yield(THREAD_ANY);
		ii++;
	} while (ret != THREAD_NONE);

	/*
	 * create maxthreads-1 threads
	 */
	printf("creating  %d threads\n", THREAD_MAX_THREADS - 1);
	for (ii = 0; ii < THREAD_MAX_THREADS - 1; ii++) {
		ret = thread_create((void (*)(void *))fact, (void *)10);
		assert(thread_ret_ok(ret));
	}
	/*
	 * Now we're out of threads. Next create should fail.
	 */
	ret = thread_create((void (*)(void *))fact, (void *)10);
	assert(ret == THREAD_NOMORE);
	/*
	 * Now let them all run.
	 */
	printf("running   %d threads\n", THREAD_MAX_THREADS - 1);
	for (ii = 0; ii < THREAD_MAX_THREADS; ii++) {
		ret = thread_yield(ii);
		if (ii == 0) {
			/* 
			 * Guaranteed that first yield will find someone. 
			 * Later ones may or may not depending on who
			 * stub schedules  on exit.
			 */
			assert(thread_ret_ok(ret));
		}
	}

	/* check that the thread stacks are sufficiently far apart */
	for (ii = 0; ii < THREAD_MAX_THREADS; ii++) {
		for (jj = 0; jj < THREAD_MAX_THREADS; jj++) {
			if (ii == jj)
				continue;
			long stack_sep = (long)(stack_array[ii]) -
				(long)(stack_array[jj]);
			if ((labs(stack_sep) < THREAD_MIN_STACK)) {
				printf("stacks of threads %d and %d "
				       "are too close\n", ii, jj);
				assert(0);
			}
		}
	}

	/*
	 * They should have cleaned themselves up when
	 * they finished running. Create maxthreads-1 threads.
	 */
	printf("creating  %d threads\n", THREAD_MAX_THREADS - 1);
	for (ii = 0; ii < THREAD_MAX_THREADS - 1; ii++) {
		child[ii] = thread_create((void (*)(void *))fact, (void *)10);
		assert(thread_ret_ok(child[ii]));
	}
	/*
	 * Now destroy some explicitly and let the others run
	 */
	printf("destroying %d threads\n", THREAD_MAX_THREADS / 2);
	for (ii = 0; ii < THREAD_MAX_THREADS; ii += 2) {
		ret = thread_kill(child[ii]);
		assert(thread_ret_ok(ret));
	}
	for (ii = 0; ii < THREAD_MAX_THREADS; ii++) {
		ret = thread_yield(ii);
	}

	ret = thread_kill(thread_id());
	assert(ret == THREAD_INVALID);
	printf("testing some destroys even though I'm the only thread\n");

	ret = thread_kill(42);
	assert(ret == THREAD_INVALID);
	ret = thread_kill(-42);
	assert(ret == THREAD_INVALID);
	ret = thread_kill(THREAD_MAX_THREADS + 1000);
	assert(ret == THREAD_INVALID);

	/*
	 * Create a thread that destroys itself. Control should come back here
	 * after that thread runs.
	 */
	printf("testing destroy self\n");
	int flag = set_flag(0);
	ret = thread_create((void (*)(void *))suicide, NULL);
	assert(thread_ret_ok(ret));
	ret = thread_yield(ret);
	assert(thread_ret_ok(ret));
	flag = set_flag(0);
	assert(flag == 1);	/* Other thread ran */
	/* That thread is gone now */
	ret = thread_yield(ret);
	assert(ret == THREAD_INVALID);

	/* check for memory leaks. at this point, the only thread that should be
	 * running is the main thread, and so no memory should have been
	 * allocated using malloc. this assumes that the thread structures are
	 * allocated statically. */
	mallinfo_check(&minfo_start);
	grand_finale();
	printf("\n\nBUG: test should not get here\n\n");
	assert(0);
}

static void
grand_finale()
{
	Tid ret;

	printf("for my grand finale, I will destroy myself\n");
	printf("while my talented assistant prints \"basic test done\"\n");
	ret = thread_create((void (*)(void *))finale, NULL);
	assert(thread_ret_ok(ret));
	thread_exit(ret);
	assert(0);

}

static void
hello(char *msg)
{
	Tid ret;
	char str[20];

	printf("message: %s\n", msg);
	ret = thread_yield(THREAD_SELF);
	assert(thread_ret_ok(ret));
	printf("thread returns from  first yield\n");

	/* we cast ret to a float because that helps to check
	 * whether the stack alignment of the frame pointer is correct */
	sprintf(str, "%3.0f\n", (float)ret);

	ret = thread_yield(THREAD_SELF);
	assert(thread_ret_ok(ret));
	printf("thread returns from second yield\n");

	while (1) {
		thread_yield(THREAD_ANY);
	}

}

static int
fact(int n)
{
	/* store address of some variable on stack */
	stack_array[thread_id()] = (long *)&n;
	if (n == 1) {
		return 1;
	}
	return n * fact(n - 1);
}

static void
suicide()
{
	int ret = set_flag(1);
	assert(ret == 0);
	thread_exit(0);
	assert(0);
}

static int flag_value;

/* sets flag_value to val, returns old value of flag_value */
static int
set_flag(int val)
{
	return __sync_lock_test_and_set(&flag_value, val);
}

static void
finale()
{
	int ret;
	printf("finale running\n");
	ret = thread_yield(THREAD_ANY);
	assert(ret == THREAD_NONE);
	ret = thread_yield(THREAD_ANY);
	assert(ret == THREAD_NONE);
	printf("basic test done\n");
	/* 
	 * Stub should exit cleanly if there are no threads left to run.
	 */
	return;
}



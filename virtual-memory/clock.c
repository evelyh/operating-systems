#include "pagetable_generic.h"
#include "sim.h"
#include <stdlib.h>


int *clock_arr;
int curr;

/* Page to evict is chosen using the CLOCK algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int clock_evict(void)
{
	while(clock_arr[curr] != 0){
		clock_arr[curr] = 0;
		curr++;
		curr %= memsize;
	}
	int past = curr;
	curr++;
	curr %= memsize;
	return past;
}

/* This function is called on each access to a page to update any information
 * needed by the CLOCK algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void clock_ref(int frame)
{
	clock_arr[frame] = 1;
}

/* Initialize any data structures needed for this replacement algorithm. */
void clock_init(void)
{
	curr = 0;
	clock_arr = malloc(sizeof(int) * memsize);
	for(int i = 0; (unsigned) i < memsize; i++){
		clock_arr[i] = 0;
	}
}

/* Cleanup any data structures created in clock_init(). */
void clock_cleanup(void)
{
	free(clock_arr);
}

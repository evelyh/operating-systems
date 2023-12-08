#include "pagetable_generic.h"
#include "sim.h"
#include <stdlib.h>

//O(1) solution

typedef struct frame_node{
	int frame_num;
	struct frame_node* prev;
	struct frame_node* next;
}frame_node;

typedef struct ref_map{
	int ref_flag;
	frame_node* node;
}ref_map;

ref_map *map; //array
frame_node *head;
frame_node *tail;


/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int lru_evict(void)
{
    frame_node *temp = head;
	int ret = temp->frame_num;

	map[ret].ref_flag = 0;
	map[ret].node = NULL;
	head = head->next;
	head->prev = NULL;

	free(temp);
	return ret;
}

/* This function is called on each access to a page to update any information
 * needed by the LRU algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(int frame)
{
	if(map[frame].ref_flag){ //referenced
		frame_node *curr = map[frame].node;
		if(curr->next != NULL){ //not in tail, moving to tail
			if(curr->prev == NULL){ //head
				head = head->next;
				curr->next->prev = NULL;
			}else{ //middle
				curr->prev->next = curr->next;
				curr->next->prev = curr->prev;
			}
			curr->prev = tail;
			tail->next = curr;
			curr->next = NULL;
			tail = curr;
		}
	}else{
		frame_node *new = malloc(sizeof(frame_node));
		new->frame_num = frame;
		new->next = NULL;
		if(head == NULL){
			new->prev = NULL;
			head = new;
		}else{
			new->prev = tail;
			tail->next = new;
		}
		tail = new;
		map[frame].ref_flag = 1;
		map[frame].node = new;
	}
}

/* Initialize any data structures needed for this replacement algorithm. */
void lru_init(void)
{
	map = malloc(sizeof(ref_map) * memsize);
	head = NULL;
	tail = NULL;
	for(int i = 0; (unsigned) i < memsize; i++){
		map[i].ref_flag = 0;
		map[i].node = NULL;
	}
}

/* Cleanup any data structures created in lru_init(). */
void lru_cleanup(void)
{
	free(map);
}


//O(M) solution

// int *last_ref_arr;
// int curr;

// /* Page to evict is chosen using the accurate LRU algorithm.
//  * Returns the page frame number (which is also the index in the coremap)
//  * for the page that is to be evicted.
//  */
// int lru_evict(void)
// {
// 	int min_i = 0;
// 	for(int i = 0; (unsigned) i < memsize; i++){
// 		if(last_ref_arr[i] < last_ref_arr[min_i]){
// 			min_i = i;
// 		}
// 	}
// 	return min_i;
// }

// /* This function is called on each access to a page to update any information
//  * needed by the LRU algorithm.
//  * Input: The page table entry for the page that is being accessed.
//  */
// void lru_ref(int frame)
// {
// 	curr++;
// 	last_ref_arr[frame] = curr;
// }

// /* Initialize any data structures needed for this replacement algorithm. */
// void lru_init(void)
// {
// 	curr = 0;
// 	last_ref_arr = malloc(sizeof(int) * memsize);
// 	for(int i = 0; (unsigned) i < memsize; i++){
// 		last_ref_arr[i] = 0;
// 	}
// }

// /* Cleanup any data structures created in lru_init(). */
// void lru_cleanup(void)
// {
// 	free(last_ref_arr);
// }
/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Andrew Peterson, Karen Reid, Alexey Khrabrov
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2021 Karen Reid
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pagetable_generic.h"
#include "pagetable.h"
#include "swap.h"

pdpt_entry_t pdpt_arr[PAGE_SIZE];

// Counters for various events.
// Your code must increment these when the related events occur.
size_t hit_count = 0;
size_t miss_count = 0;
size_t ref_count = 0;
size_t evict_clean_count = 0;
size_t evict_dirty_count = 0;

/*
 * Allocates a frame to be used for the virtual page represented by p.
 * If all frames are in use, calls the replacement algorithm's evict_func to
 * select a victim frame. Writes victim to swap if needed, and updates
 * page table entry for victim to indicate that virtual page is no longer in
 * (simulated) physical memory.
 *
 * Counters for evictions should be updated appropriately in this function.
 */
static int allocate_frame(pt_entry_t *pte)
{
	int frame = -1;
	for (size_t i = 0; i < memsize; ++i) {
		if (!coremap[i].in_use) {
			frame = i;
			break;
		}
	}

	if (frame == -1) { // Didn't find a free page.
		// Call replacement algorithm's evict function to select victim
		frame = evict_func();
		assert(frame != -1);

		// All frames were in use, so victim frame must hold some page
		// Write victim page to swap, if needed, and update page table

		pt_entry_t *vic_pte = coremap[frame].pte;

		if(vic_pte->frame & DIRTY_BIT){
			off_t off = swap_pageout(frame, vic_pte->swap_offset);
			if(off == INVALID_SWAP){
				fprintf(stderr, "invalid swap out");
				exit(1);
			}
			vic_pte->swap_offset = off;
			evict_dirty_count++;
		}else{
			evict_clean_count++;
		}
		vic_pte->frame |= SWAP_ON;
		vic_pte->frame &= ~DIRTY_BIT;
		vic_pte->frame &= ~VALID_BIT;
	}

	// Record information for virtual page that will now be stored in frame
	coremap[frame].in_use = true;
	coremap[frame].pte = pte;

	return frame;
}

/*
 * Initializes your page table.
 * This function is called once at the start of the simulation.
 * For the simulation, there is a single "process" whose reference trace is
 * being simulated, so there is just one overall page table.
 *
 * In a real OS, each process would have its own page table, which would
 * need to be allocated and initialized as part of process creation.
 * 
 * The format of the page table, and thus what you need to do to get ready
 * to start translating virtual addresses, is up to you. 
 */

//top level
void init_pagetable(void)
{
	for(int i = 0; i < PAGE_SIZE; i++){
		pdpt_arr[i].pdpe = 0;
	}
}
//second level
pdpt_entry_t init_pagedir(void){
	pdt_entry_t *pdt_arr;
	if(posix_memalign((void **)&pdt_arr, PAGE_SIZE, PAGE_SIZE * sizeof(pdt_entry_t)) != 0){
		perror("malloc for page dir");
		exit(1);
	}
	for(int i = 0; i < PAGE_SIZE; i++){
		pdt_arr[i].pde = 0;
	}
	pdpt_entry_t new_pdpt;
	uintptr_t pdt = (uintptr_t) pdt_arr;
	new_pdpt.pdpe = pdt | VALID_BIT;
	return new_pdpt;
}

//third level
pdt_entry_t init_pagetbl(void){
	pt_entry_t *pt_arr;
	if(posix_memalign((void **)&pt_arr, PAGE_SIZE, PAGE_SIZE * sizeof(pt_entry_t)) != 0){
		perror("malloc for page table");
		exit(1);
	}
	for(int i = 0; i < PAGE_SIZE; i++){
		pt_arr[i].swap_offset = INVALID_SWAP;
		pt_arr[i].frame = 0;
	}
	pdt_entry_t new_pdt;
	uintptr_t pt = (uintptr_t) pt_arr;
	new_pdt.pde = pt | VALID_BIT;
	return new_pdt;
}

/*
 * Initializes the content of a (simulated) physical memory frame when it
 * is first allocated for some virtual address. Just like in a real OS, we
 * fill the frame with zeros to prevent leaking information across pages.
 */
static void init_frame(int frame)
{
	// Calculate pointer to start of frame in (simulated) physical memory
	unsigned char *mem_ptr = &physmem[frame * SIMPAGESIZE];
	memset(mem_ptr, 0, SIMPAGESIZE); // zero-fill the frame
}

/*
 * Locate the physical frame number for the given vaddr using the page table.
 *
 * If the page table entry is invalid and not on swap, then this is the first 
 * reference to the page and a (simulated) physical frame should be allocated 
 * and initialized to all zeros (using init_frame).
 *
 * If the page table entry is invalid and on swap, then a (simulated) physical 
 * frame should be allocated and filled by reading the page data from swap.
 *
 * When you have a valid page table entry, return the start of the page frame
 * that holds the requested virtual page.
 *
 * Counters for hit, miss and reference events should be incremented in
 * this function.
 */
unsigned char *find_physpage(vaddr_t vaddr, char type)
{
	int frame = -1; // Frame used to hold vaddr

	// Use your page table to find the page table entry (pte) for the 
	// requested vaddr. 

	//top
	unsigned int pdpt_index = (((vaddr) >> PDPT_SHIFT));
	if(!(pdpt_arr[pdpt_index].pdpe & VALID_BIT)){
		pdpt_arr[pdpt_index] = init_pagedir();
	}
	uintptr_t pdpe = (pdpt_arr[pdpt_index].pdpe & PAGE_MASK);
	
	//second
	unsigned int pdt_index = (((vaddr) >> PDT_SHIFT) & ~PAGE_MASK);
	pdt_entry_t *pdt_arr = (pdt_entry_t *) pdpe;
	if(!(pdt_arr[pdt_index].pde & VALID_BIT)){
		pdt_arr[pdt_index] = init_pagetbl();
	}
	uintptr_t pde = (pdt_arr[pdt_index].pde & PAGE_MASK);

	//third
	unsigned int pt_index = (((vaddr) >> PAGE_SHIFT) & ~PAGE_MASK);
	pt_entry_t *pte = (pt_entry_t *) pde + pt_index;

	// Check if pte is valid or not, on swap or not, and handle appropriately.
	// You can use the allocate_frame() and init_frame() functions here,
	// as needed.

	// Make sure that pte is marked valid and referenced. Also mark it
	// dirty if the access type indicates that the page will be written to.
	// (Note that a page should be marked DIRTY when it is first accessed, 
	// even if the type of first access is a read (Load or Instruction type).

	if(!(pte->frame & VALID_BIT)){
		frame = allocate_frame(pte);
		if(pte->frame & SWAP_ON){
			if(swap_pagein(frame, pte->swap_offset) != 0){
				fprintf(stderr, "invalid swap in");
				exit(1);
			}
			pte->frame = (frame << PAGE_SHIFT);
			pte->frame |= SWAP_ON;
			pte->frame &= ~DIRTY_BIT;
		}else{
			init_frame(frame);
			pte->frame = (frame << PAGE_SHIFT);
			pte->frame |= DIRTY_BIT;
		}
		miss_count++;
	}else{
		frame = (pte->frame >> PAGE_SHIFT);
		hit_count++;
	}

	pte->frame |= (VALID_BIT);
	pte->frame |= (REF_BIT);
	if (type == 'S' || type == 'M'){
		pte->frame |= (DIRTY_BIT);
	}
	ref_count++;

	// Call replacement algorithm's ref_func for this page.
	assert(frame != -1);
	ref_func(frame);

	// Return pointer into (simulated) physical memory at start of frame
	return &physmem[frame * SIMPAGESIZE];
}

void print_pagetbl(pt_entry_t *pt_arr){
	for(int i = 0; i < PAGE_SIZE; i++){
		int val = pt_arr[i].frame & VALID_BIT;
		int swp = pt_arr[i].frame & SWAP_ON;
		int dty = pt_arr[i].frame & DIRTY_BIT;
		if(val && swp){
			if(val){ //valid
				if(dty){ //dirty
					printf("valid, dirty frame %d\n", pt_arr[i].frame >> PAGE_SHIFT);
				}else if(!dty){ //not dirty
					printf("valid frame %d\n", pt_arr[i].frame >> PAGE_SHIFT);
				}
			}else{ //swapon
				printf("swapon frame %d, offset %lu\n", pt_arr[i].frame >> PAGE_SHIFT, pt_arr[i].swap_offset);
			}
		}
	}
}

void print_pagedir(pdt_entry_t *pdt_arr){
	for(int i = 0; i < PAGE_SIZE; i++){
		if(pdt_arr[i].pde & VALID_BIT){
			uintptr_t pt_a = (pdt_arr[i].pde & PAGE_MASK);
			pt_entry_t *pt_arr = (pt_entry_t *)pt_a;
			printf("pdt %d, pt %p\n", i, pt_arr);
			print_pagetbl(pt_arr);
		}
	}
}

void print_pagetable(void)
{
	for (int i = 0; i < PAGE_SIZE; i++){
		if (pdpt_arr[i].pdpe & VALID_BIT){
			printf("valid: %d\n",i);
			uintptr_t pdt_a = (pdpt_arr[i].pdpe & PAGE_MASK);
			pdt_entry_t *pdt_arr = (pdt_entry_t *)pdt_a;
			printf("pdpt %d, pdt %p\n", i, pdt_arr);
			print_pagedir(pdt_arr);
		}
	}
}


void free_pagetable(void)
{
	for(int i = 0; i < PAGE_SIZE; i++){
		if(pdpt_arr[i].pdpe & VALID_BIT){
			uintptr_t pdt_a = (pdpt_arr[i].pdpe & PAGE_MASK);
			pdt_entry_t *pdt_arr = (pdt_entry_t *) pdt_a;
			for(int j = 0; j < PAGE_SIZE; j++){
				if(pdt_arr[j].pde & VALID_BIT){
					uintptr_t pt_a = (pdt_arr[j].pde & PAGE_MASK);
					pt_entry_t *pt_arr = (pt_entry_t *) pt_a;
					free(pt_arr);
				}
			}
			free(pdt_arr);
		}
	}
}
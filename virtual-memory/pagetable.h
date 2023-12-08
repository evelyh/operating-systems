#ifndef __PAGETABLE_H__
#define __PAGETABLE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>


// User-level virtual addresses on a 64-bit Linux system are 48 bits in our
// traces, and the page size is 4096 (12 bits). The remaining 36 bits are
// the virtual page number, which is used as the lookup key (or index) into
// your page table. 

//Page directory pointer table entry - top level

typedef struct pdpt_entry_s{
	uintptr_t pdpe;
}pdpt_entry_t;

//Page directory table entry - second level
typedef struct pdt_entry_s{
	uintptr_t pde;
}pdt_entry_t;

// Page table entry - third level
// This structure will need to record the physical page frame number
// for a virtual page, as well as the swap offset if it is evicted. 
// You will also need to keep track of the Valid, Dirty and Referenced
// status bits (or flags). 
// You do not need to keep track of Read/Write/Execute permissions.
typedef struct pt_entry_s {
	unsigned int frame;
	off_t swap_offset;
}pt_entry_t;

#endif /* __PAGETABLE_H__ */

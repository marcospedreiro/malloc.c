#include <errno.h>
#include <limits.h>
#include <string.h>
//
#include <unistd.h> 
#include <stdio.h>
//
#include "malloc.h"
#include "memreq.h"

// Define Header at the beginning of each slice of memory
union Header {
	struct {
		union Header *prev; // Previous sliceOfMem
		union Header *next; // Next sliceOfMem
		size_t size;        // total size of sliceOfMem, INCLUDING HEADER (need to subtract sizeof(union Header) to get actual slice size)
		int flag;          // sliceOfMem flag 
	} loafOfMem;
	long alignment; // force alignment 
};

// List of sliceOfMems, sorted by order of increasing addresses 
// Initialize List pointers
static union Header *L_head;
static union Header *L_tail; 

// HELPER FUNCTIONS //
void splitSlice(union Header *sliceOfMem, size_t required_sliceOfMem_size);
void combineSlices(union Header *sliceOfMem);
//////////////////////

// MALLOC
void *malloc(size_t size)
{
	size_t required_sliceOfMem_size;
	union Header *sliceOfMem;

	// Get the minimum sliceOfMem size needed 
	// Round given size up to the nearest multiple
	if((size + sizeof(union Header)) % sizeof(union Header) != 0) {
		required_sliceOfMem_size = (size + sizeof(union Header)) + (sizeof(union Header) - ((size + sizeof(union Header)) % sizeof(union Header)));
	} else {
		required_sliceOfMem_size = (size + sizeof(union Header));
	}
	
	// search list for a big enough unallocated sliceOfMem 
	// Causes segfault, why?
	/*union Header *iter = L_head;
	int foundSlice = 0;
	while(iter->loafOfMem.next != NULL && (!foundSlice)) {
		if (iter->loafOfMem.size >= required_sliceOfMem_size && !iter->loafOfMem.flag) {
			foundSlice = 1;
			sliceOfMem = iter;
		} else {
			iter = iter->loafOfMem.next;
		}
	} */
	for (sliceOfMem = L_head; sliceOfMem != 0; sliceOfMem = sliceOfMem->loafOfMem.next) {
		if (sliceOfMem->loafOfMem.size >= required_sliceOfMem_size && !sliceOfMem->loafOfMem.flag) { 
			break;
		}
	} 

	if (sliceOfMem == NULL) { // no big enough sliceOfMem found
		// try to allocate newSlice
		union Header *newSlice = NULL;

		// get_memory
		newSlice = (union Header*)get_memory((intptr_t) required_sliceOfMem_size); 

		// Append newSlice to list 
		newSlice->loafOfMem.next = NULL;
		if (L_head == NULL) {
			// first allocation 
			L_head = newSlice;
			L_tail = newSlice;
			newSlice->loafOfMem.prev = NULL;
		} else {
			// append sliceOfMem to tail
			L_tail->loafOfMem.next = newSlice;
			newSlice->loafOfMem.prev = L_tail;
			L_tail = newSlice;
		}
		newSlice->loafOfMem.size = required_sliceOfMem_size;
		newSlice->loafOfMem.flag = 0;

		sliceOfMem = newSlice;
		if (sliceOfMem == NULL) { // new sliceOfMem couldn't be allocated
			errno = ENOMEM;
			return NULL;
		}
	}

	// sliceOfMem must be split if its size exceeds required_sliceOfMem_size + sizeof(union Header) 
	if(sliceOfMem->loafOfMem.size > (required_sliceOfMem_size + sizeof(union Header))) {
		splitSlice(sliceOfMem, required_sliceOfMem_size);
	}
	
	sliceOfMem->loafOfMem.flag = 1; // mark as allocated

	return (void*) (sliceOfMem + 1); // sets pointer to start of memory (skip header)
}


static size_t highest(size_t in) {
    size_t num_bits = 0;

    while (in != 0) {
        ++num_bits;
        in >>= 1;
    }

    return num_bits;
}


void *calloc(size_t number, size_t size) 
{
	size_t number_size = 0;

    /* This prevents an integer overflow.  A size_t is a typedef to an integer
     * large enough to index all of memory.  If we cannot fit in a size_t, then
     * we need to fail.
     */
    if (highest(number) + highest(size) > sizeof(size_t) * CHAR_BIT) {
        errno = ENOMEM;
        return NULL;
    }

    number_size = number * size;
    void* ret = malloc(number_size);

    if (ret) {
        memset(ret, 0, number_size);
    }

    return ret;
}


void *realloc(void *ptr, size_t size)
{
	if(ptr == NULL) {
		return malloc(size);
	} 

	union Header *sliceOfMem;
	sliceOfMem = ((union Header *)ptr) - 1;
	size_t old_size = sliceOfMem->loafOfMem.size - sizeof(union Header); // XXX Set this to the size of the buffer pointed to by ptr 
	
	void* ret = malloc(size);

    if (ptr) {
        if (ret) {
            memmove(ret, ptr, old_size < size ? old_size : size);
            free(ptr);
        }

        return ret;
    } else {
        errno = ENOMEM;
        return NULL;
    } 
}



void free(void *ptr)
{
	if (ptr == NULL) { // Do nothing for null pointer
		return;
	}

	union Header *sliceOfMem;

	// find header 
	sliceOfMem = ((union Header *)ptr) - 1;

	// mark sliceOfMem as being free 
	sliceOfMem->loafOfMem.flag = 0; 

	// Try to combine sliceOfMems with prev and next 
	// | p | cur | n |
	combineSlices(sliceOfMem->loafOfMem.prev); // | p | cur | --> | p + cur |
	combineSlices(sliceOfMem); // | cur | n | --> | cur + n |
	
}

//////////////////////////////////////////////////////////////////////
/************************* HELPER FUNCTIONS *************************/
//////////////////////////////////////////////////////////////////////

/** Function to split given sliceOfMem if (leftOver space - required_sliceOfMem_size)
 *  is big enough to form a new slice
 *  Inputs: Slice to be split, required_sliceOfMem_size
 *  Outputs: None (hopefully correctly splits slices)
**/
void splitSlice(union Header *sliceOfMem, size_t required_sliceOfMem_size)
{
	union Header *leftOver;
	size_t leftOver_Size;
 
	leftOver_Size = sliceOfMem->loafOfMem.size - required_sliceOfMem_size;
	if (leftOver_Size <= sizeof(union Header)) { // not enough space for new slice
		return;
	}

	sliceOfMem->loafOfMem.size = required_sliceOfMem_size; // change size of original

	leftOver = (union Header *) (((char *) sliceOfMem) + required_sliceOfMem_size); // get address of leftOver slice

	leftOver->loafOfMem.size = leftOver_Size; // Get the new header
	leftOver->loafOfMem.flag = 0; // init to be free

	// insert leftOver as sliceOfMem->loafOfMem.next
	leftOver->loafOfMem.next = sliceOfMem->loafOfMem.next;
	leftOver->loafOfMem.prev = sliceOfMem;
	if (sliceOfMem->loafOfMem.next != NULL) {
		sliceOfMem->loafOfMem.next->loafOfMem.prev = leftOver;
	} else {
		L_tail = leftOver; // tail was split, leftOver is new tail
	}
	sliceOfMem->loafOfMem.next = leftOver;
} 

/** Function to combine given sliceOfMem with its nextSlice if needed
 *  This is my 'coalescer'
 *  Input: Slice to be combined
 *  Output: None (hopefully correctly updates list)
**/
void combineSlices(union Header *sliceOfMem)
{
	union Header *nextSlice;

	if (sliceOfMem == NULL) {
		return;
	}
	nextSlice = sliceOfMem->loafOfMem.next;
	
	// make sure nextSlice exists
	if(nextSlice == NULL) {
		return;
	}

	// make sure both sliceOfMem and nextSlice are unflagged (free)
	if ((!sliceOfMem->loafOfMem.flag) || (!nextSlice->loafOfMem.flag)) { 
		return;
	}

	// combine nextSlice and sliceOfMem 
	sliceOfMem->loafOfMem.size += nextSlice->loafOfMem.size;

	// remove nextSlice from list
	if (nextSlice->loafOfMem.next != NULL) {
		nextSlice->loafOfMem.next->loafOfMem.prev = sliceOfMem; // nextSlice->next must have sliceOfMem as its prev
	} else {
		L_tail = sliceOfMem; // nextSlice was tail, make sliceOfMem tail 
	}
	sliceOfMem->loafOfMem.next = sliceOfMem->loafOfMem.next->loafOfMem.next;
}





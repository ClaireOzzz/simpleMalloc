#include "cs15_malloc.h"

void* freeList = NULL;
void* topChunk = NULL;
void* memStart = (void*)(0xdafeda1e000);
size_t pageSize = 4096;

static void nullify_next_two(void* ptr) {
	void** ptrptr = (void**)(ptr);
	ptrptr[1] = NULL;
	ptrptr[2] = NULL;
}

struct chunkresult {
	char touchingUpperChunk;
	char touchingLowerChunk;
	void* nearestUpperChunk;
	void* nearestLowerChunk;
};

struct chunkresult freeListHelper(void* ptr) {
	// traverse freelist
	struct chunkresult cr = { 0, 0, NULL, NULL };
	void* end = (char*)ptr + *(size_t*)ptr + 24;

	for (void* c = freeList; c != NULL ; c = *((void**)c + 2)){
		void* freeListChunkEnd = (char*)c + *(size_t*)c + 24;
		if (c == end) {
			cr.touchingUpperChunk = 1;
                        cr.nearestUpperChunk = c;
		}
		else if (freeListChunkEnd == ptr) {
			cr.touchingLowerChunk = 1;
                        cr.nearestLowerChunk = c;
		}

	}
	return cr;
}


void* cs15_malloc(size_t size) {
	
	size_t rounded_size = (size + 7) & ~(size_t)7; // round up to 8 bytes
	if (rounded_size == 0) { rounded_size = 8; }

	void* result_ptr;
	size_t chunkSize = rounded_size; // to use at the end

	// case 0: no memory mapped yet
	if (topChunk == NULL) {
		size_t bytes = ((rounded_size + 48 + pageSize - 1) / pageSize) * pageSize;
		void* mmapped_ptr = mmap(
                          	memStart,
			  	bytes,
                          	PROT_READ | PROT_WRITE,
                          	MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE,
                          	-1,
                          	0);

		if (mmapped_ptr == MAP_FAILED) { return NULL; }
		result_ptr = (char*)mmapped_ptr + 24; 

		topChunk = (char*)result_ptr + rounded_size;
		nullify_next_two(topChunk);
		*(size_t*)topChunk = bytes - rounded_size - 48;
	}

	else {
		// case 1: first-fit search of the freelist
		void* curr = freeList;
		while (curr != NULL && *(size_t*)curr < rounded_size) {
			curr = *((void**)curr + 2);
		}

		if (curr != NULL) {
			size_t currSize = *(size_t*)curr;
			void* currPrev = *((void**)curr + 1);
			void* currNext = *((void**)curr + 2);

			if (currSize >= rounded_size + 24 + 8) {    // split: remainder takes curr's place
				void* remainder = (char*)curr + 24 + rounded_size;
				*(size_t*)remainder = currSize - rounded_size - 24;
				*((void**)remainder + 1) = currPrev;
				*((void**)remainder + 2) = currNext;
				if (currPrev != NULL) { *((void**)currPrev + 2) = remainder; }
				else                  { freeList = remainder; }
				if (currNext != NULL) { *((void**)currNext + 1) = remainder; }
			} else {                                     // no split: unlink
				chunkSize = currSize;
				if (currPrev != NULL) { *((void**)currPrev + 2) = currNext; }
				else                  { freeList = currNext; }
				if (currNext != NULL) { *((void**)currNext + 1) = currPrev; }
			}
			result_ptr = (char*)curr + 24;
		}

		// case a: carve from topChunk
		else if (*(size_t*)topChunk >= rounded_size + 24) {
			size_t oldTopSize = *(size_t*)topChunk;
			result_ptr = (char*)topChunk + 24;
			topChunk = (char*)topChunk + 24 + rounded_size;
			*(size_t*)topChunk = oldTopSize - rounded_size - 24;
			nullify_next_two(topChunk);
		}

		// case b: map more pages after the top chunk
		else {
			size_t oldTopSize = *(size_t*)topChunk;
			size_t quot = (rounded_size + 24 - oldTopSize + pageSize - 1) / pageSize;
			void* endOfChunk = (char*)topChunk + 24 + oldTopSize;
			void* grown = mmap(endOfChunk, pageSize * quot, PROT_READ | PROT_WRITE,
					   MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);
			if (grown == MAP_FAILED) { return NULL; }

			result_ptr = (char*)topChunk + 24;
			topChunk = (char*)topChunk + 24 + rounded_size;
			*(size_t*)topChunk = oldTopSize + pageSize * quot - rounded_size - 24;
			nullify_next_two(topChunk);
		}
	}

	void* result_head = (char*)result_ptr - 24;
	*(size_t*)result_head = chunkSize; 
	nullify_next_two(result_head);
	return result_ptr;
}

void cs15_free(void * freeptr) {
	
	if(freeptr == NULL) { return; }
	
	// Q: do we need to check for valid freeptr? ie not already freed, not some rando location like a prev ptr
	
	void* freeptrStart = (char*)freeptr - 24;
	size_t freeptrSize = *(size_t*)(freeptrStart);
	void** freeptrNext = (void**)freeptrStart + 2;
	void** freeptrPrev = (void**)freeptrStart + 1;
	
	struct chunkresult cr = freeListHelper(freeptrStart);
	char inList = 0; // could use bool but would need a library

	// case 1: freeList NULL
	if (freeList == NULL) {
		freeList = freeptrStart;
		*(size_t*)freeList = freeptrSize;
		nullify_next_two(freeList);
		inList = 1;
	}
	
	// case 2: forward coalesce
	if (cr.touchingUpperChunk) {
		void* upperChunkPtr = cr.nearestUpperChunk;
		size_t upperChunkSize = *(size_t*)upperChunkPtr;
		void* upperChunkNext = *((void**)upperChunkPtr + 2);
		void* upperChunkPrev = *((void**)upperChunkPtr + 1);

		// merge
		*freeptrNext = upperChunkNext;
		*freeptrPrev = upperChunkPrev;

		// update neighbours
		if (upperChunkPrev != NULL) {
			void** upperChunkTopNeighbour = (void**)upperChunkPrev + 2;
			*upperChunkTopNeighbour = freeptrStart;
		}
		else { freeList = freeptrStart; } // if top chunk was freelist head

		if (upperChunkNext != NULL) {
			void** upperChunkBottomNeighbour = (void**)upperChunkNext + 1;
			*upperChunkBottomNeighbour = freeptrStart;
		}
		inList = 1;
		freeptrSize = upperChunkSize + freeptrSize + 24;
  		*(size_t*)freeptrStart = freeptrSize;
	}

	// case 3: backward coalesce
	if (cr.touchingLowerChunk) {
		void* lowerChunkPtr = cr.nearestLowerChunk;
		size_t lowerChunkSize = *(size_t*)lowerChunkPtr;

		// if freeptr already in list, unlink
		if (inList) {
			void* freeptrPrevChunk = *freeptrPrev;
			void* freeptrNextChunk = *freeptrNext;
			if (freeptrPrevChunk != NULL) {
				void** prevprevChunk = (void**)freeptrPrevChunk + 2;
				*prevprevChunk = freeptrNextChunk;
			}
			else { freeList = freeptrNextChunk; }

			if (freeptrNextChunk != NULL) {
				void** nextnextChunk = (void**)freeptrNextChunk + 1;
				*nextnextChunk = freeptrPrevChunk;
			}
		}
		
		// merge
		freeptrSize = lowerChunkSize + freeptrSize + 24;
		freeptrStart = lowerChunkPtr;
		*(size_t*)freeptrStart = freeptrSize;
		// resetting some ptrs
		freeptrPrev = (void**)(freeptrStart) + 1;
                freeptrNext = (void**)(freeptrStart) + 2;
                inList = 1;
	}

	// case 4: freeptr directly below topChunk
	void* freeptrNextChunk = (char*)freeptrStart + freeptrSize + 24;
	if (freeptrNextChunk == topChunk) {
		size_t oldTopSize = *(size_t*)topChunk;
                char*  topEnd = (char*)topChunk + 24 + oldTopSize;

		// should not be in list if merged with topchunk
		if (inList) {
                        void* freeptrPrevChunk = *freeptrPrev;
                        void* freeptrNextInList = *freeptrNext;
                        if (freeptrPrevChunk != NULL) { 
				*(void**)((char*)freeptrPrevChunk + 16) = freeptrNextInList;	}
                        else { freeList = freeptrNextInList; }
                        if (freeptrNextInList != NULL) { 
				*(void**)((char*)freeptrNextInList + 8) = freeptrPrevChunk;
			}
                }

		topChunk = freeptrStart;
                *(size_t*)topChunk = freeptrSize + 24 + oldTopSize;   // NEW top size, not "+=" on itself
                nullify_next_two(topChunk);
	
		if (topChunk == memStart) {
                        // only the top chunk is left
                        munmap(memStart, topEnd - (char*)memStart);
                        topChunk = NULL;
                        freeList = NULL;
		} 
		else {
                        // keep whatever page(s) hold the top header, unmap whole pages past that
                        size_t keepEnd = ((size_t)topChunk + 24 + pageSize - 1) / pageSize * pageSize;
                        if (keepEnd < (size_t)topEnd) {
                                munmap((void*)keepEnd, (size_t)topEnd - keepEnd);
                                *(size_t*)topChunk = keepEnd - (size_t)topChunk - 24;
                        }
                }
		return;
	}

	// case 5: isolated chunk not already in list
	if (!inList) {
                *freeptrPrev = NULL;
                *freeptrNext = freeList;      // NULL if the list was empty
                if (freeList != NULL) { *((void**)freeList + 1) = freeptrStart; }   // old head's prev
                freeList = freeptrStart;
        }
}


void* cs15_realloc(void* ptr, size_t size) {
	if (ptr == NULL) { return cs15_malloc(size); }
	if (size <= 0 ) {
		cs15_free(ptr);
		return NULL;
	}

	void* temp_alloc = cs15_malloc(size);
	if (temp_alloc == NULL) {return NULL;}
	size_t curr_size = *(size_t*)((char*)ptr - 24); // go to header
	size_t bytes_to_copy = (curr_size < size) ? curr_size : size;

	char* src = (char*)ptr;
	char* dest = (char*)temp_alloc;

	for (size_t i=0; i<bytes_to_copy; i++) {
		dest[i] = src[i];
	}

	cs15_free(ptr);
	return temp_alloc;
}


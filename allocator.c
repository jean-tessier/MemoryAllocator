/*
   	Memory Allocator
*/
#include "allocator.h"

/* Global vars */
static int zerofd;
static int initialized = 0;


/* Contains mapped pages */
static void *page[MAX_ALLOC-MIN_ALLOC];

/* Called when library is unloaded */
void cleanup(void){

}

/* Called when library is loaded */
void init(void){
	//Used to initialize memory to zero
	zerofd = open("/dev/zero", O_RDWR);
	initialized = 1;
}

/* 
	Allocates a single block of memory during the execution of the program.
	
	size = size of the memory block in bytes

	C Standard malloc() Does not initialize the allocated memory (garbage values will be found)
	Currently, this malloc() initializes the allocated memory to 0, until I find a way around this.	

	Returns null ptr if unable to allocate memory 
	Returns void ptr to allocated memory space
*/
void *malloc(size_t size) {
	if(initialized == 0) init();

	double d_size = size;
	d_size = fmod(log2(size),1)*10;

	/* This calculates which page to store the malloc'd space in (currently assumes the size is at most 1024) */
	int target_page = log2(size)+(d_size>0? 1 : 0);
	size = pow( 2, target_page );

	/* If the malloc size is >1024, we will map a page specifically for that object. */
	if(size+sizeof(small_header_t) > 1024 ||
		size+sizeof(small_header_t)+sizeof(header_t) > 1024) {

		size = size+sizeof(small_header_t);
		int s = 4096;
		int i = 1;
		while(s<size) s = s*i++;
		size = s;
		void *new_page = mmap(NULL, size,
				      PROT_READ | PROT_WRITE,
				      MAP_PRIVATE, zerofd, 0);
		if(new_page == MAP_FAILED) return NULL;
		/* The small header is on all pages, so for consistency we'll put it on this page too,
		   though only page_size is necessary. */
		small_header_t *small_header = new_page;
		small_header->block_count = 1;
		small_header->page_size = size+1;
		small_header->next_page = NULL;
		small_header->prev_page = NULL;
		/* Return the pointer to the first allocatable space */
		return (void *)( (uint8_t *)new_page+sizeof(small_header_t) );
	}	

	target_page -= MIN_ALLOC;
	/* Ensures target page is, at minimum, 0 (negative values would be silly.)  */
	if(target_page < 0) target_page = 0;	
	
	/* By default, no pages are mapped (that is, all pages == NULL)
	   This maps the initial page if necessary */
	if(page[target_page]==NULL) {
		page[target_page] = mmap(NULL, PAGE_SIZE,
					 PROT_READ | PROT_WRITE,
					 MAP_PRIVATE, zerofd, 0);
		if(page[target_page] == MAP_FAILED) return NULL;
		
		/* The number of blocks allocated on this page. When 0, we'll unmap it. 
		   We'll set it to one to count this allocation. */
		size_t *block_count = page[target_page];
		*block_count = 1;
		/* The size of this page, so we can unmap it correctly later.
		   It is independent of header_t so we can use it without wasting space where a header_t is unneccessary. */
		size_t *page_size = (void *)( (uint8_t *)block_count+sizeof(size_t) );
		*page_size = PAGE_SIZE;
		
		void **next_page = (void *)( (uint8_t *)page_size+sizeof(size_t) );
		*next_page = NULL;
		void **prev_page = (void *)( (uint8_t *)next_page+PTR_SIZE );
		*prev_page = NULL;		

		header_t *header = (void *)( (uint8_t *)prev_page+PTR_SIZE );
		/* Free will only be utilized after something has been freed. */
		//header->free = NULL; //TODO: remove
		/* Point alloc to first address after this header */
		header->alloc = (void *)( (uint8_t *)(header)+sizeof(header_t) );		
		/* Start counting allocated blocks so we don't walk off the page. */
		header->alloc_count = 1;
		/* Calculate the size of a block on this page and store it for later use. */
		//header->block_size = pow(2, log2(size));//target_page+MIN_ALLOC);
		header->block_size = size;
		/* Calculate the maximum allocations per page here, that way we only have to do it once. */
		header->alloc_max = ( PAGE_SIZE-sizeof(small_header_t)-sizeof(header_t) )/( header->block_size+PTR_SIZE );
		/* Count the pages for this size */
		header->page_count = 1;

		/* Place a pointer to the next available 'node' just after header->alloc  */	
		void **next_node = (void *)( (uint8_t *)(header->alloc)+header->block_size );

		*next_node = (void *)( (uint8_t *)next_node+PTR_SIZE );	
		/* Now that the page is initialized, return the first allocatable space for user's use.  */
		return header->alloc;
	}

	/* If this point is reached, the page header and the page are initialized and may be used.
	   The following code needs the header information to execute, so we get that info here. */
	header_t *header = (void *)( (uint8_t *)page[target_page]+sizeof(small_header_t) );
	/* TODO: If a previously allocated space has been freed on this page, we'll give the user that space. */
/*VVV I'm here VVV*/
/*	if(header->free != NULL) {		
		void *node = header->free
		void *next_node = (void *)( (uint8_t *)(node)+header->block_size );

		while (*next_node != NULL) {
			node = (void *)( (uint8_t *)(node) + header->block_size+PTR_SIZE );
			next_node = (void *)( (uint8_t *)(node) + header->block_size );
		}

		*next_node = (void *)( (uint8_t *)(next_node)+PTR_SIZE );		

		//void *node = header->alloc;
		//void **next_node = (void *)( (uint8_t *)(node)+header->block_size );
	
		// Iterate to the end of the alloc list. We'll remove the block from the free list and place it there. 	
		//while (*next_node != NULL) {
		//	node = (void *)( (uint8_t *)node + header->block_size+PTR_SIZE );
		//	next_node = (void *)( (uint8_t *)node + header->block_size );	
		//}
		
		return node;
	} */
	
	/* If we're at the end of our page and there are no freed blocks available, we need to map a new page.
	   This implementation preemptively creates a new page, so it CAN be wasteful in edge cases (such as when
	   the user JUST fills up the first page.) */
	size_t all_page_alloc_max = header->alloc_max + ( (header->page_count-1) * ( ( (PAGE_SIZE-sizeof(small_header_t) )/(header->block_size+PTR_SIZE) ) ) );

	if( header->alloc_count+1 >= all_page_alloc_max ) {
		/* Map a new page for this size. We shouldn't need to keep track of it, freeing can be done mathematically. */
		void *new_page = mmap(NULL, PAGE_SIZE,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE, zerofd, 0);
		if(new_page == MAP_FAILED) return NULL;
		/* Set the small header for this page.
		   We'll set block count to 0, since we won't use it until the next allocation of this size. 
		   Though block count is 0, the page won't be unmapped until allocations are made on this page 
		   and then all allocations on this page are freed. */
		small_header_t *small_header = new_page;
		small_header->block_count = 0;//this change may mess things up <-------------------------------------
		small_header->page_size = PAGE_SIZE;
		small_header->next_page = NULL;
		/* we'll set prev_page at the bottom of this if */
	
		/* These represent our list nodes. */
		void *node = header->alloc;
		void **next_node = (void *)( (uint8_t *)(node)+header->block_size );	
	
		/* Traverse the list to the final node of the page (should be the final node, because alloc_count == alloc_max */	
		//this might mess things up <-----------------------------------------------------------------------
		void *last_page = page[target_page];
		small_header_t *last_page_small_header = page[target_page];
		while (last_page_small_header->next_page != NULL)
			last_page = last_page_small_header->next_page;
		if(last_page == page[target_page])
			next_node = (void *)( (uint8_t *)last_page+(header->alloc_max*(header->block_size+PTR_SIZE))-PTR_SIZE );
		else
			next_node = (void *)( (uint8_t *)last_page+(header->alloc_max*(header->block_size+PTR_SIZE))+sizeof(header_t)-PTR_SIZE );
		//next_node = (void *)( (uint8_t *)new_page)
		//final_next_node = (void *)( (uint8_t *)prev_page+(prev_page_alloc_max*(header->block_size+PTR_SIZE))-PTR_SIZE );
		//*final_next_node = NULL;

		//for (int i=0; i<all_page_alloc_max; i++) {
		//	node = (void *)( (uint8_t *)(node)+header->block_size+PTR_SIZE );
		//	next_node = (void *)( (uint8_t *)(node)+header->block_size );
		//}	

		/* Now we set the next node to be at the first allocatable block of the page we mapped. */
		*next_node = (void *)( (uint8_t *)new_page+sizeof(small_header_t) );

		/* I've added a page, so we need to count it. */
		++header->page_count;
		++header->alloc_count;	
	
		/* Now, we need to track node's page's next page (this one) */
		void *node_page = (void *)( (uintptr_t)node & ~( (uintptr_t)0xFFF ) );
		small_header_t *node_page_sm_header = node_page;
		//void **node_page_next = (void *)( (uint8_t *)node_page + (sizeof(size_t)*2) );
		
		//size_t *node_page_block_count = (void *)( (uint8_t *)node_page );		

		small_header->prev_page = node_page;
		node_page_sm_header = new_page;
		++node_page_sm_header->block_count;
		//++(*node_page_block_count);
	
		/* Return the newly allocated memory block */	
		return node;
	}
	
	/* Otherwise, we'll give the user the next allocatable space on the page. */
	else {
		/* Node and next_node refer to our 'list nodes' */
		void **next_node = (void *)( (uint8_t *)header->alloc+header->block_size);
		void *node = *next_node;

		next_node = (void *)( (uint8_t *)(node)+header->block_size );		
		//node = (void *)( (uint8_t *)(next_node)+PTR_SIZE );
		//return node;
	
		/* Iterate through our nodes until we get to an unused block. */
		while (*next_node != NULL) {
			node = (void *)( (uint8_t *)node+header->block_size+PTR_SIZE );
			next_node = (void *)( (uint8_t *)node+header->block_size );
		}
	
		/* Get the page this node is allocated on so we can count it on said page. */
		void *page_ptr = (void *)( (uintptr_t)node & ~( (uintptr_t)0xFFF ) );
		small_header_t *page_sm_header = page_ptr;
		//size_t *block_count = page_ptr;
	
		/* Initialize this node by linking it to the next available block. */	
		*next_node = (void *)( (uint8_t *)(node)+header->block_size+PTR_SIZE );

		/* Count this block as allocated. */
		++header->alloc_count;
		++page_sm_header->block_count;
		//++(*block_count);

		/* Return the memory block we have allocated for the user */
		return node;
	}
	
	return NULL;
}

/*
	Allocates multiple blocks of memory
	
	nitems	= # elements to be allocated
	size 	= size of the elements

	Memory is initialized to zero
	
	Returns null ptr if unable to allocate memory
	Returns void ptr to allocated memory space (How does this work with multiple blocks of memory?)
*/
void *calloc(size_t nitems, size_t size) {
	size_t total_size = nitems*size;
	return malloc(total_size);
}

/* 
	Attempts to resize the memory block pointed to by ptr that was previously allocated with a call to malloc or calloc.

	ptr 	= pointer to a memory block previously allocated with malloc, calloc or realloc to be reallocated.
			  If NULL, a new block is allocated and a pointer to it is returned by the function.
	size 	= new size for the memory block, in bytes. If 0, and ptr points to an existing block of 
			  memory, the memory block pointed by ptr is deallocated and a NULL pointer is returned.
*/
void *realloc(void *ptr, size_t size){
	if(ptr == NULL)
		return malloc(size);

	if(size == 0) {
		free(ptr);
		return NULL;
	}
	
	void **header_page = (void *)( (uintptr_t)ptr & ~( (uintptr_t)0xFFF ) );
	header_page = (void *)( (uint8_t *)header_page+(sizeof(size_t)*2)+PTR_SIZE);
	
	while(*header_page != NULL) {
		header_page = *header_page;
		header_page = (void *)( (uint8_t *)header_page+(sizeof(size_t)*2)+PTR_SIZE);
	}

	header_t *header = (void *)( (uint8_t *)header_page+PTR_SIZE+sizeof(header_t));

	if(size <= header->block_size)
		return ptr;

	void *new_ptr = malloc(size);
	if(header->block_size > size) memcpy(new_ptr, ptr, header->block_size);	
	else memcpy(new_ptr, ptr, size);
	return new_ptr;
}

/* 
	Frees the memory location specified by passed ptr.
	Undefined behaviour if specified memory location already freed (likely crash) 

	pointer	= pointer to a memory block previously allocated with malloc, calloc or realloc to be deallocated.
			  If NULL no action occurs.
*/

/* TODO: add a first page pointer to get the header, otherwise we'll get odd results */
void free(void *ptr){
	if(ptr == NULL) return;
		
	/* Get the pointer to the head of the page
	   Then, get the page header(s). */
    void *page_ptr = (void *)( (uintptr_t)ptr & ~( (uintptr_t)0xFFF ) );	
	small_header_t *small_header = page_ptr;
	//size_t *page_size = (void *)( (uint8_t *)page_ptr + sizeof(size_t) );	

	/* If page size is greater than 1024, it was allocated independently. Simply unmap it. */	
	if( small_header->page_size > PAGE_SIZE) {
		--small_header->page_size;
		munmap( page_ptr, small_header->page_size );
		page_ptr = NULL;
		return;
	}
	
	//size_t *block_count = page_ptr;
	header_t* header = (void *)( (uint8_t *)page_ptr+sizeof(small_header_t) ); /* TODO: This needs to be fixed. Header is not on all pages, just the first. */

	/* Decrement our block counters; this block is freed. */	
	
	--header->alloc_count;
	--small_header->block_count;
	//*block_count = *block_count-1;

	/* If this page has no more allocated blocks, there is no need for it -- unmap it. 
	   Doing this removes the ptr from mapped space, freeing it.
	   We should set the previous nodes 'next' pointer to NULL to remove this node. */
	if(small_header->block_count <= 0) { /* The page pointing to this page should point to the next page. Doubly linked list, maybe? */
		//void *prev_page = (void *)( (uint8_t *)page_ptr+(sizeof(size_t)*2)+PTR_SIZE);
		if(small_header->prev_page != NULL){
			small_header_t *prev_page_sm_header = small_header->prev_page;
			void *prev_page_prev = prev_page_sm_header->prev_page;
			//void **prev_page_prev = (void *)( (uint8_t *)prev_page+(sizeof(size_t)*2)+PTR_SIZE);
			void **final_next_node;
			size_t prev_page_alloc_max;

			/* if this is the first page */
			if(prev_page_prev == NULL) {
				prev_page_alloc_max = (PAGE_SIZE-sizeof(small_header_t)-sizeof(header_t))/(header->block_size+PTR_SIZE);	
			}
			else {
				prev_page_alloc_max = (PAGE_SIZE-sizeof(small_header_t))/(header->block_size+PTR_SIZE);	
			}

			final_next_node = (void *)( (uint8_t *)small_header->prev_page+(prev_page_alloc_max*(header->block_size+PTR_SIZE))-PTR_SIZE );
			*final_next_node = NULL;
		}
		int page_num = log2(header->block_size)-MIN_ALLOC;
		if(page_num < 0)
			page_num = 0;
		page[page_num] = NULL;
		
		munmap( page_ptr, small_header->page_size );
		return;
	}
	/* If we're here, this page remains mapped, so we'll set the freed node so it can be allocated again.
	  Get ptr->next_node */
	void **ptr_next = (void *)( (uint8_t *)ptr+header->block_size );

	/* Since we're placing ptr at the end of our freelist, ptr_next is set to NULL, to represent that. */
	*ptr_next = NULL;
	return;
}
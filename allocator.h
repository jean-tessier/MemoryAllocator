#define _GNU_SOURCE

void __attribute__ ((constructor)) init(void);
void __attribute__ ((destructor)) cleanup(void);

#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
/* 4K page size */
#define PAGE_SIZE 4096
/* pointer size,  */
#define PTR_SIZE sizeof(void*)
//minimum allocation size 2^3 (8) bytes
#define MIN_ALLOC 3 
//maximum allocation size (before own chunk) 2^10 (1024) bytes
#define MAX_ALLOC 10

#define SMALL_HEADER (sizeof(size_t)*2)+(PTR_SIZE*2)

typedef struct {
	size_t block_count;
	size_t page_size;
	void *next_page;
	void *prev_page;
} small_header_t;

typedef struct {
	void *alloc; //8
	size_t block_size; //8
	size_t alloc_count; //8
	size_t alloc_max; //8
	size_t page_count; //8
} header_t;

/* Function definitions */
void *malloc(size_t size);
void *calloc(size_t nitems, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
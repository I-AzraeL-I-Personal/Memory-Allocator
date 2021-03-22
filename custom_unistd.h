#if !defined(_CUSTOM_UNISTD_H_)
#define _CUSTOM_UNISTD_H_

#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

void* custom_sbrk(intptr_t delta);
int heap_setup(void);
void* heap_malloc(size_t count);
void* heap_calloc(size_t number, size_t size);
void  heap_free(void* memblock);
void* heap_realloc(void* memblock, size_t size);
void* heap_malloc_debug(size_t count, int fileline, const char* filename);
void* heap_calloc_debug(size_t number, size_t size, int fileline, const char* filename);
void* heap_realloc_debug(void* memblock, size_t size, int fileline, const char* filename);
void* heap_malloc_aligned(size_t count);
void* heap_calloc_aligned(size_t number, size_t size);
void* heap_realloc_aligned(void* memblock, size_t size);
void* heap_malloc_aligned_debug(size_t count, int fileline, const char* filename);
void* heap_calloc_aligned_debug(size_t number, size_t size, int fileline, const char* filename);
void* heap_realloc_aligned_debug(void* memblock, size_t size, int fileline, const char* filename);
size_t   heap_get_used_space(void);
size_t   heap_get_largest_used_block_size(void);
uint64_t heap_get_used_blocks_count(void);
size_t   heap_get_free_space(void);
size_t   heap_get_largest_free_area(void);
uint64_t heap_get_free_gaps_count(void);
enum pointer_type_t get_pointer_type(const void* pointer);
void* heap_get_data_block_start(const void* pointer);
size_t heap_get_block_size(const void* memblock);
int heap_validate(void);
void heap_dump_debug_information(void);

struct block_meta {
    uint8_t start_fence;
    bool empty;
    bool debug;
    int fileline;
    struct block_meta *prev;
    struct block_meta *next;
    size_t size;
    char filename[31];
    uint8_t end_fence;
};

enum pointer_type_t {
    pointer_null,
    pointer_out_of_heap,
    pointer_control_block,
    pointer_inside_data_block,
    pointer_unallocated,
    pointer_valid
};

#if defined(sbrk)
#undef sbrk
#endif

#if defined(brk)
#undef brk
#endif


#define sbrk(__arg__) (assert("Proszę nie używać standardowej funkcji sbrk()" && 0), (void*)-1)
#define brk(__arg__) (assert("Proszę nie używać standardowej funkcji sbrk()" && 0), -1)

#endif // _CUSTOM_UNISTD_H_

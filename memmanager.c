#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h>
#include <pthread.h>
#include "custom_unistd.h"

#define PAGE_SIZE       4096    // Długość strony w bajtach
#define PAGE_FENCE      1       // Liczba stron na jeden płotek
#define PAGES_AVAILABLE 16384   // Liczba stron dostępnych dla sterty
#define PAGES_TOTAL     (PAGES_AVAILABLE + 2 * PAGE_FENCE)

#define malloc(_size) heap_malloc_debug((_size), __LINE__, __FILE__)
#define calloc(_number, _size) heap_calloc_debug((_number), (_size), __LINE__, __FILE__)
#define realloc(_ptr, _size) heap_realloc_debug((_ptr), (_size), __LINE__, __FILE__)
#define malloc_aligned(_size) heap_malloc_aligned_debug((_size), __LINE__, __FILE__)
#define calloc_aligned(_number, _size) heap_calloc_aligned_debug((_number), (_size), __LINE__, __FILE__)
#define realloc_aligned(_ptr, _size) heap_realloc_aligned_debug((_ptr), (_size), __LINE__, __FILE__)
#define META_SIZE (sizeof(struct block_meta))
#define DATA_PTR(META_PTR) (((intptr_t) META_PTR) + META_SIZE)
#define START_VAL 85    //01010101
#define END_VAL 170     //10101010

uint8_t memory[PAGE_SIZE * PAGES_TOTAL] __attribute__((aligned(PAGE_SIZE)));

struct block_meta *heap = NULL;
pthread_mutex_t mut;

struct memory_fence_t {
    uint8_t first_page[PAGE_SIZE];
    uint8_t last_page[PAGE_SIZE];
};

struct mm_struct {
    intptr_t start_brk;
    intptr_t brk;
    
    // Poniższe pola nie należą do standardowej struktury mm_struct
    struct memory_fence_t fence;
    intptr_t start_mmap;
} mm;

void __attribute__((constructor)) memory_init(void)
{
    //
    // Inicjuj testy
    setvbuf(stdout, NULL, _IONBF, 0); 
    srand(time(NULL));
    assert(sizeof(intptr_t) == sizeof(void*));
    
    /*
     * Architektura przestrzeni dynamicznej dla sterty, z płotkami pamięci:
     * 
     *  |<-   PAGES_AVAILABLE            ->|
     * ......................................
     * FppppppppppppppppppppppppppppppppppppL
     * 
     * F - płotek początku
     * L - płotek końca
     * p - strona do użycia (liczba stron nie jest znana)
     */
    
    //
    // Inicjuj płotki
    for (int i = 0; i < PAGE_SIZE; i++) {
        mm.fence.first_page[i] = rand();
        mm.fence.last_page[i] = rand();
    }
    
    //
    // Ustaw płotki
    memcpy(memory, mm.fence.first_page, PAGE_SIZE);
    memcpy(memory + (PAGE_FENCE + PAGES_AVAILABLE) * PAGE_SIZE, mm.fence.last_page, PAGE_SIZE);

    //
    // Inicjuj strukturę opisującą pamięć procesu (symulację tej struktury)
    mm.start_brk = (intptr_t)(memory + PAGE_SIZE);
    mm.brk = (intptr_t)(memory + PAGE_SIZE);
    mm.start_mmap = (intptr_t)(memory + (PAGE_FENCE + PAGES_AVAILABLE) * PAGE_SIZE);
    
    assert(mm.start_mmap - mm.start_brk == PAGES_AVAILABLE * PAGE_SIZE);
} 

void __attribute__((destructor)) memory_check(void)
{
    //
    // Sprawdź płotki
    int first = memcmp(memory, mm.fence.first_page, PAGE_SIZE);
    int last = memcmp(memory + (PAGE_FENCE + PAGES_AVAILABLE) * PAGE_SIZE, mm.fence.last_page, PAGE_SIZE);
    
    printf("\n### Stan płotków przestrzeni sterty:\n");
    printf("    Płotek początku: [%s]\n", first == 0 ? "poprawny" : "USZKODZONY");
    printf("    Płotek końca...: [%s]\n", last == 0 ? "poprawny" : "USZKODZONY");

    printf("### Podsumowanie: \n");
        printf("    Całkowita przestrzeni pamięci....: %lu bajtów\n", mm.start_mmap - mm.start_brk);
        printf("    Pamięć zarezerwowana przez sbrk(): %lu bajtów\n", mm.brk - mm.start_brk);
    
    //if (first || last) {
        printf("Naciśnij ENTER...");
        fgetc(stdin);
    //}
}

//
//
//


void* custom_sbrk(intptr_t delta)
{
    intptr_t current_brk = mm.brk;
    if (mm.start_brk + delta < 0) {
        errno = 0;
        return (void*)current_brk;
    }
    
    if (mm.brk + delta >= mm.start_mmap) {
        errno = ENOMEM;
        return (void*)-1;
    }
    mm.brk += delta;
    return (void*)current_brk;
}

int heap_setup(void) {
    if(heap != NULL && heap_validate() != 0)
        return -1;
    size_t pages;
    if(heap != NULL) { //RESET MODE
        pages = (heap_get_used_space() + heap_get_free_space()) / PAGE_SIZE;
        for(size_t i = 0; i < pages - 1; ++i)
            custom_sbrk(-PAGE_SIZE);
        heap->size = PAGE_SIZE - sizeof(struct block_meta);
        heap->prev = NULL;
        heap->next = NULL;
        heap->empty = true;
        heap->start_fence = START_VAL;
        heap->end_fence = END_VAL;
        return 0;
    }
    pthread_mutex_init(&mut, NULL);
    heap = custom_sbrk(PAGE_SIZE);
    if((void *)heap == (void *)-1)
        return -1;
    heap->size = PAGE_SIZE - sizeof(struct block_meta);
    heap->prev = NULL;
    heap->next = NULL;
    heap->empty = true;
    heap->start_fence = START_VAL;
    heap->end_fence = END_VAL;
    return 0;
}

void* heap_malloc(size_t count) {
    if(!count)
        return NULL;
    pthread_mutex_lock(&mut);
    struct block_meta *curr = heap;
    struct block_meta *last = heap;
    struct block_meta *next_old;

    // FIND EMPTY BLOCK AND SPLIT (IF NEEDED)
    while(curr) {
        if(curr->empty && (curr->size > count + META_SIZE || curr->size == count)) {
            curr->empty = false;

            if(curr->size > count + META_SIZE) {
                next_old = curr->next;
                curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
                if(next_old)
                    next_old->prev = curr->next;
                curr->next->empty = true;
                curr->next->size = curr->size - META_SIZE - count;
                curr->next->next = next_old;
                curr->next->prev = curr;
                curr->next->end_fence = END_VAL;
                curr->next->start_fence = START_VAL;
                curr->next->debug = false;
            }

            curr->size = count;
            curr->debug = false;
            pthread_mutex_unlock(&mut);
            return (void *)DATA_PTR(curr);
        }
        last = curr;
        curr = curr->next;
    }
    //

    //IF NOT FOUND, INCREASE HEAP SIZE AND SPLIT
    curr = last;
    struct block_meta *ptr;
    size_t alloc_size = ceil((double)(count - curr->size + META_SIZE) / PAGE_SIZE) * PAGE_SIZE;
    ptr = custom_sbrk(alloc_size);
    if((void *)ptr == (void *)-1) {
        pthread_mutex_unlock(&mut);
        return NULL;
    }
    curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
    curr->empty = false;
    ptr->start_fence = START_VAL;
    ptr->end_fence = END_VAL;

    curr->next->empty = true;
    curr->next->next = NULL;
    curr->next->prev = curr;
    curr->next->size = alloc_size - (count - curr->size) - META_SIZE;
    curr->size = count;
    curr->next->start_fence = START_VAL;
    curr->next->end_fence = END_VAL;
    curr->next->debug = false;
    curr->debug = false;
    //
    pthread_mutex_unlock(&mut);
    return (void *)DATA_PTR(curr);
}

void* heap_calloc(size_t number, size_t size) {
    size_t count = number * size;
    void *ptr = heap_malloc(count);
    if(!ptr)
        return NULL;
    memset(ptr, 0, count);
    return ptr;
}

void  heap_free(void* memblock) {
    if(!memblock)
        return;
    pthread_mutex_init(&mut, NULL);
    struct block_meta *block = (struct block_meta *)((intptr_t)memblock - META_SIZE);
    block->empty = true;

    //MERGE BLOCKS
    struct block_meta *temp;
    block = heap->next;
    while(block) {
        if(block->prev->empty && block->empty) {
            if(block->next)
                block->next->prev = block->prev;
            block->prev->next = block->next;
            block->prev->size += block->size + META_SIZE;
            block = block->prev;
        }
        block = block->next;
    }
    //

    //RETURN MEMORY
    block = heap;
    intptr_t count = 0;
    while(block->next)
        block = block->next;
    if(block->empty && block->size > PAGE_SIZE) {
        count = block->size / PAGE_SIZE * PAGE_SIZE;
        while(count) {
            custom_sbrk(-PAGE_SIZE);
            count -= PAGE_SIZE;
        }
        block->size = block->size % PAGE_SIZE;
    }
    //
    pthread_mutex_unlock(&mut);
}

void* heap_realloc(void* memblock, size_t size) {
    if(!memblock)
        return heap_malloc(size);
    if(!size) {
        heap_free(memblock);
        return memblock;
    }
    void *new_block = heap_malloc(size);
    struct block_meta *block_meta = (struct block_meta *)((intptr_t)memblock - META_SIZE);
    size_t copy_size;
    if(new_block) {
        (block_meta->size > size) ? (copy_size = size) : (copy_size = block_meta->size);
        memcpy(new_block, memblock, copy_size);
        block_meta->empty = true;
        heap_free(memblock);
    }
    return new_block;
}

void* heap_malloc_debug(size_t count, int fileline, const char* filename) {
    if(!count)
        return NULL;
    pthread_mutex_lock(&mut);
    struct block_meta *curr = heap;
    struct block_meta *last = heap;
    struct block_meta *next_old;

    // FIND EMPTY BLOCK AND SPLIT (IF NEEDED)
    while(curr) {
        if(curr->empty && (curr->size > count + META_SIZE || curr->size == count)) {
            curr->empty = false;

            if(curr->size > count + META_SIZE) {
                next_old = curr->next;
                curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
                if(next_old)
                    next_old->prev = curr->next;
                curr->next->empty = true;
                curr->next->size = curr->size - META_SIZE - count;
                curr->next->next = next_old;
                curr->next->prev = curr;
                curr->next->end_fence = END_VAL;
                curr->next->start_fence = START_VAL;
                curr->next->debug = false;
            }

            curr->size = count;
            memset(curr->filename, 0, 31);
            memcpy(curr->filename, filename, 30);
            curr->debug = true;
            curr->fileline = fileline;
            pthread_mutex_unlock(&mut);
            return (void *)DATA_PTR(curr);
        }
        last = curr;
        curr = curr->next;
    }
    //

    //IF NOT FOUND, INCREASE HEAP SIZE AND SPLIT
    curr = last;
    struct block_meta *ptr;
    size_t alloc_size = ceil((double)(count - curr->size + META_SIZE) / PAGE_SIZE) * PAGE_SIZE;
    ptr = custom_sbrk(alloc_size);
    if((void *)ptr == (void *)-1) {
        pthread_mutex_unlock(&mut);
        return NULL;
    }
    curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
    curr->empty = false;
    ptr->start_fence = START_VAL;
    ptr->end_fence = END_VAL;

    curr->next->empty = true;
    curr->next->next = NULL;
    curr->next->prev = curr;
    curr->next->size = alloc_size - (count - curr->size) - META_SIZE;
    curr->size = count;
    curr->next->start_fence = START_VAL;
    curr->next->end_fence = END_VAL;
    curr->next->debug = false;
    memset(curr->filename, 0, 31);
    memcpy(curr->filename, filename, 30);
    curr->debug = true;
    curr->fileline = fileline;
    //
    pthread_mutex_unlock(&mut);
    return (void *)DATA_PTR(curr);
}

void* heap_calloc_debug(size_t number, size_t size, int fileline, const char* filename) {
    size_t count = number * size;
    void *ptr = heap_malloc_debug(count, fileline, filename);
    if(!ptr)
        return NULL;
    memset(ptr, 0, count);
    return ptr;
}

void* heap_realloc_debug(void* memblock, size_t size, int fileline, const char* filename) {
    if(!memblock)
        return heap_malloc_debug(size, fileline, filename);
    if(!size) {
        heap_free(memblock);
        return memblock;
    }
    void *new_block = heap_malloc_debug(size, fileline, filename);
    struct block_meta *block_meta = (struct block_meta *)((intptr_t)memblock - META_SIZE);
    size_t copy_size;
    if(new_block) {
        (block_meta->size > size) ? (copy_size = size) : (copy_size = block_meta->size);
        memcpy(new_block, memblock, copy_size);
        block_meta->empty = true;
        heap_free(memblock);
    }
    return new_block;
}

void* heap_malloc_aligned(size_t count) {
    if(!count)
        return NULL;
    pthread_mutex_lock(&mut);
    struct block_meta *curr = heap;
    struct block_meta *last = heap;
    struct block_meta *next_old, *ret_block;

    int offset;
    // FIND EMPTY BLOCK AND SPLIT (IF NEEDED)
    while(curr) {
        offset = PAGE_SIZE - META_SIZE - ((intptr_t)curr & (intptr_t)(PAGE_SIZE - 1));
        if(curr->empty && (curr->size >= offset + META_SIZE + count + META_SIZE || curr->size == offset + count + META_SIZE || curr->size == count)) {
            next_old = curr->next;

            if(offset != 0) {
                curr->next = (struct block_meta *)((intptr_t)curr + offset);
                ret_block = curr->next;
                if(next_old)
                    next_old->prev = curr->next;
                curr->next->empty = false;
                curr->next->size = curr->size - offset - META_SIZE;
                curr->next->next = next_old;
                curr->next->prev = curr;
                curr->next->end_fence = END_VAL;
                curr->next->start_fence = START_VAL;
                curr->next->debug = false;
                curr->size = offset - META_SIZE;
                if(curr->next->size > count) {
                    curr = curr->next;
                    curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
                    if(next_old)
                        next_old->prev = curr->next;
                    curr->next->empty = true;
                    curr->next->size = curr->size - count;
                    curr->size = count;
                    curr->next->next = next_old;
                    curr->next->prev = curr;
                    curr->next->end_fence = END_VAL;
                    curr->next->start_fence = START_VAL;
                    curr->next->debug = false;
                }
            }
            else {
                ret_block = curr;
                if(curr->size > count) {
                    curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
                    if(next_old)
                        next_old->prev = curr->next;
                    curr->next->empty = true;
                    curr->next->size = curr->size - META_SIZE - count;
                    curr->next->next = next_old;
                    curr->next->prev = curr;
                    curr->next->end_fence = END_VAL;
                    curr->next->start_fence = START_VAL;
                    curr->next->debug = false;
                    curr->size = count;
                }
                curr->empty = false;
            }

            ret_block->debug = false;
            pthread_mutex_unlock(&mut);
            return (void *)DATA_PTR(ret_block);
        }
        last = curr;
        curr = curr->next;
    }
    //

    //IF NOT FOUND, INCREASE HEAP SIZE AND SPLIT
    curr = last;
    offset = PAGE_SIZE - META_SIZE - ((intptr_t)curr & (intptr_t)(PAGE_SIZE - 1));
    struct block_meta *ptr;
    size_t alloc_size = ceil((double)(META_SIZE + count + META_SIZE + offset - curr->size) / PAGE_SIZE) * PAGE_SIZE;
    ptr = custom_sbrk(alloc_size);
    if((void *)ptr == (void *)-1) {
        pthread_mutex_unlock(&mut);
        return NULL;
    }
    curr->size += alloc_size;
    if(offset != 0) {
        curr->next = (struct block_meta *)((intptr_t)curr + offset);
        ret_block = curr->next;
        curr->next->empty = false;
        curr->next->size = curr->size - offset;
        curr->next->next = NULL;
        curr->next->prev = curr;
        curr->next->end_fence = END_VAL;
        curr->next->start_fence = START_VAL;
        curr->next->debug = false;
        curr->size = offset - META_SIZE;
        if(curr->next->size > count) {
            curr = curr->next;
            curr->next = (struct block_meta *)((intptr_t)curr + META_SIZE + count);
            curr->next->empty = true;
            curr->next->size = curr->size - count - META_SIZE;
            curr->size = count;
            curr->next->next = NULL;
            curr->next->prev = curr;
            curr->next->end_fence = END_VAL;
            curr->next->start_fence = START_VAL;
            curr->next->debug = false;
        }
    }
    else {
        ret_block = curr;
        if(curr->size > count) {
            curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
            curr->next->empty = true;
            curr->next->size = curr->size - META_SIZE - count;
            curr->next->next = NULL;
            curr->next->prev = curr;
            curr->next->end_fence = END_VAL;
            curr->next->start_fence = START_VAL;
            curr->next->debug = false;
            curr->size = count;
        }
        curr->empty = false;
    }

    ret_block->debug = false;
    //
    pthread_mutex_unlock(&mut);
    return (void *)DATA_PTR(ret_block);
}

void* heap_calloc_aligned(size_t number, size_t size) {
    size_t count = number * size;
    void *ptr = heap_malloc_aligned(count);
    if(!ptr)
        return NULL;
    memset(ptr, 0, count);
    return ptr;
}

void* heap_realloc_aligned(void* memblock, size_t size) {
    if(!memblock)
        return heap_malloc_aligned(size);
    if(!size) {
        heap_free(memblock);
        return memblock;
    }
    void *new_block = heap_malloc_aligned(size);
    struct block_meta *block_meta = (struct block_meta *)((intptr_t)memblock - META_SIZE);
    size_t copy_size;
    if(new_block) {
        (block_meta->size > size) ? (copy_size = size) : (copy_size = block_meta->size);
        memcpy(new_block, memblock, copy_size);
        block_meta->empty = true;
        heap_free(memblock);
    }
    return new_block;
}

void* heap_malloc_aligned_debug(size_t count, int fileline, const char* filename) {
    if(!count)
        return NULL;
    pthread_mutex_lock(&mut);
    struct block_meta *curr = heap;
    struct block_meta *last = heap;
    struct block_meta *next_old, *ret_block;

    int offset;
    // FIND EMPTY BLOCK AND SPLIT (IF NEEDED)
    while(curr) {
        offset = PAGE_SIZE - META_SIZE - ((intptr_t)curr & (intptr_t)(PAGE_SIZE - 1));
        if(curr->empty && (curr->size >= offset + META_SIZE + count + META_SIZE || curr->size == offset + count + META_SIZE || curr->size == count)) {
            next_old = curr->next;

            if(offset != 0) {
                curr->next = (struct block_meta *)((intptr_t)curr + offset);
                ret_block = curr->next;
                if(next_old)
                    next_old->prev = curr->next;
                curr->next->empty = false;
                curr->next->size = curr->size - offset - META_SIZE;
                curr->next->next = next_old;
                curr->next->prev = curr;
                curr->next->end_fence = END_VAL;
                curr->next->start_fence = START_VAL;
                curr->next->debug = false;
                curr->size = offset - META_SIZE;
                if(curr->next->size > count) {
                    curr = curr->next;
                    curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
                    if(next_old)
                        next_old->prev = curr->next;
                    curr->next->empty = true;
                    curr->next->size = curr->size - count;
                    curr->size = count;
                    curr->next->next = next_old;
                    curr->next->prev = curr;
                    curr->next->end_fence = END_VAL;
                    curr->next->start_fence = START_VAL;
                    curr->next->debug = false;
                }
            }
            else {
                ret_block = curr;
                if(curr->size > count) {
                    curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
                    if(next_old)
                        next_old->prev = curr->next;
                    curr->next->empty = true;
                    curr->next->size = curr->size - META_SIZE - count;
                    curr->next->next = next_old;
                    curr->next->prev = curr;
                    curr->next->end_fence = END_VAL;
                    curr->next->start_fence = START_VAL;
                    curr->next->debug = false;
                    curr->size = count;
                }
                curr->empty = false;
            }

            ret_block->debug = true;
            ret_block->fileline = fileline;
            memset(ret_block->filename, 0, 31);
            memcpy(ret_block->filename, filename, 30);
            pthread_mutex_unlock(&mut);
            return (void *)DATA_PTR(ret_block);
        }
        last = curr;
        curr = curr->next;
    }
    //

    //IF NOT FOUND, INCREASE HEAP SIZE AND SPLIT
    curr = last;
    offset = PAGE_SIZE - META_SIZE - ((intptr_t)curr & (intptr_t)(PAGE_SIZE - 1));
    struct block_meta *ptr;
    size_t alloc_size = ceil((double)(META_SIZE + count + META_SIZE + offset - curr->size) / PAGE_SIZE) * PAGE_SIZE;
    ptr = custom_sbrk(alloc_size);
    if((void *)ptr == (void *)-1) {
        pthread_mutex_unlock(&mut);
        return NULL;
    }
    curr->size += alloc_size;
    if(offset != 0) {
        curr->next = (struct block_meta *)((intptr_t)curr + offset);
        ret_block = curr->next;
        curr->next->empty = false;
        curr->next->size = curr->size - offset;
        curr->next->next = NULL;
        curr->next->prev = curr;
        curr->next->end_fence = END_VAL;
        curr->next->start_fence = START_VAL;
        curr->next->debug = false;
        curr->size = offset - META_SIZE;
        if(curr->next->size > count) {
            curr = curr->next;
            curr->next = (struct block_meta *)((intptr_t)curr + META_SIZE + count);
            curr->next->empty = true;
            curr->next->size = curr->size - count - META_SIZE;
            curr->size = count;
            curr->next->next = NULL;
            curr->next->prev = curr;
            curr->next->end_fence = END_VAL;
            curr->next->start_fence = START_VAL;
            curr->next->debug = false;
        }
    }
    else {
        ret_block = curr;
        if(curr->size > count) {
            curr->next = (struct block_meta *)((intptr_t)curr + count + META_SIZE);
            curr->next->empty = true;
            curr->next->size = curr->size - META_SIZE - count;
            curr->next->next = NULL;
            curr->next->prev = curr;
            curr->next->end_fence = END_VAL;
            curr->next->start_fence = START_VAL;
            curr->next->debug = false;
            curr->size = count;
        }
        curr->empty = false;
    }

    ret_block->debug = true;
    ret_block->fileline = fileline;
    memset(ret_block->filename, 0, 31);
    memcpy(ret_block->filename, filename, 30);
    //
    pthread_mutex_unlock(&mut);
    return (void *)DATA_PTR(ret_block);
}

void* heap_calloc_aligned_debug(size_t number, size_t size, int fileline, const char* filename) {
    size_t count = number * size;
    void *ptr = heap_malloc_aligned_debug(count, fileline, filename);
    if(!ptr)
        return NULL;
    memset(ptr, 0, count);
    return ptr;
}

void* heap_realloc_aligned_debug(void* memblock, size_t size, int fileline, const char* filename) {
    if(!memblock)
        return heap_malloc_aligned_debug(size, fileline, filename);
    if(!size) {
        heap_free(memblock);
        return memblock;
    }
    void *new_block = heap_malloc_aligned_debug(size, fileline, filename);
    struct block_meta *block_meta = (struct block_meta *)((intptr_t)memblock - META_SIZE);
    size_t copy_size;
    if(new_block) {
        (block_meta->size > size) ? (copy_size = size) : (copy_size = block_meta->size);
        memcpy(new_block, memblock, copy_size);
        block_meta->empty = true;
        heap_free(memblock);
    }
    return new_block;
}

size_t   heap_get_used_space(void) {
    size_t size = 0;
    struct block_meta *temp = heap;
    while(temp) {
        if(!temp->empty)
            size += temp->size;
        size += META_SIZE;
        temp = temp->next;
    }
    return size;
}

size_t   heap_get_largest_used_block_size(void) {
    size_t size = 0;
    struct block_meta *temp = heap;
    while(temp) {
        if(temp->size > size && !temp->empty)
            size = temp->size;
        temp = temp->next;
    }
    return size;
}

uint64_t heap_get_used_blocks_count(void) {
    uint64_t count = 0;
    struct block_meta *temp = heap;
    while(temp) {
        if(!temp->empty)
            ++count;
        temp = temp->next;
    }
    return count;
}

size_t   heap_get_free_space(void) {
    size_t size = 0;
    struct block_meta *temp = heap;
    while(temp) {
        if(temp->empty)
            size += temp->size;
        temp = temp->next;
    }
    return size;
}

size_t   heap_get_largest_free_area(void) {
    size_t size = 0;
    struct block_meta *temp = heap;
    while(temp) {
        if(temp->size > size && temp->empty)
            size = temp->size;
        temp = temp->next;
    }
    return size;
}

uint64_t heap_get_free_gaps_count(void) {
    uint64_t count = 0;
    struct block_meta *temp = heap;
    while(temp) {
        if(temp->empty && temp->size >= sizeof(intptr_t))
            ++count;
        temp = temp->next;
    }
    return count;
}

enum pointer_type_t get_pointer_type(const void* pointer) {
    if(!pointer)
        return pointer_null;
    if((intptr_t)pointer < (intptr_t)heap || (intptr_t)pointer >= (intptr_t)heap + heap_get_used_space() + heap_get_free_space())
        return pointer_out_of_heap;
    struct block_meta *temp = heap;
    while(temp) {
        if((intptr_t)pointer >= (intptr_t)temp && (intptr_t)pointer < (intptr_t)temp + META_SIZE)
            return pointer_control_block;
        if(!temp->empty && ((intptr_t)pointer > DATA_PTR(temp) && (intptr_t)pointer < DATA_PTR(temp) + temp->size))
            return pointer_inside_data_block;
        if(temp->empty && ((intptr_t)pointer >= DATA_PTR(temp) && (intptr_t)pointer < DATA_PTR(temp) + temp->size))
            return pointer_unallocated;
        temp = temp->next;
    }
    return pointer_valid;
}

void* heap_get_data_block_start(const void* pointer) {
    enum pointer_type_t type = get_pointer_type(pointer);
    struct block_meta *temp = heap;
    if(type == pointer_inside_data_block) {
        while(temp) {
            if(!temp->empty && (intptr_t)pointer > DATA_PTR(temp) && (intptr_t)pointer < DATA_PTR(temp) + temp->size)
                return (void *)DATA_PTR(temp);
            temp = temp->next;
        }
    }
    if(type == pointer_valid)
        return (void *)pointer;
    return NULL;
}

size_t heap_get_block_size(const void* memblock) {
    enum pointer_type_t type = get_pointer_type(memblock);
    struct block_meta *temp = heap;
    if(type == pointer_valid) {
        while(temp) {
            if((intptr_t)DATA_PTR(temp) == (intptr_t)memblock)
                return temp->size;
            temp = temp->next;
        }
    }
    return 0;
}

int heap_validate(void) {
    /*
     0  OK
    -1  invalid pointer
    -2  invalid heap fences
    -3  invalid structure fences
    */
    if(!heap)
        return -1;
    int first = memcmp(memory, mm.fence.first_page, PAGE_SIZE);
    int last = memcmp(memory + (PAGE_FENCE + PAGES_AVAILABLE) * PAGE_SIZE, mm.fence.last_page, PAGE_SIZE);
    if(first != 0 || last != 0)
        return -2;
    if(heap->prev != NULL)
        return -1;
    struct block_meta *ptr = heap;
    struct block_meta *ptr_prev = heap;
    int counterFW = 0;
    int counterBW = 0;
    while(ptr) {
        if(ptr->start_fence != START_VAL || ptr->end_fence != END_VAL)
            return -3;
        if(((intptr_t)(ptr->next) != ((intptr_t)ptr + META_SIZE + ptr->size)) && ptr->next != NULL)
            return -1;
        ++counterFW;
        ptr_prev = ptr;
        ptr = ptr->next;
    }

    ptr = ptr_prev;
    while(ptr) {
        ++counterBW;
        ptr = ptr->prev;
    }
    if(counterFW != counterBW)
        return -1;
    return 0;
}

void heap_dump_debug_information(void) {
    struct block_meta *ptr = heap;
    while(ptr) {
            printf("Block address: %p, size: %zu", (void *)DATA_PTR(ptr), ptr->size);
        if(ptr->debug && !ptr->empty)
            printf(", allocated in: %s, line: %d", ptr->filename, ptr->fileline);
        if(ptr->empty)
            printf(", EMPTY");
        printf("\n");
        ptr = ptr->next;
    }
    printf("Total heap size: %zu B\n", heap_get_used_space() + heap_get_free_space());
    printf("Bytes in use: %zu B\n", heap_get_used_space());
    printf("Bytes free: %zu B\n", heap_get_free_space());
    printf("Size of the largest empty block: %zu B\n", heap_get_largest_free_area());
}
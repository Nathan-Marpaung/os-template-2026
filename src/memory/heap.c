#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "header/memory/heap.h"
#include "header/memory/paging.h"
#include "header/process/process.h"

/*
 * Heap block header. Stored inline in the heap memory, blocks form a doubly
 * linked list sorted by address so that adjacent free blocks can be merged
 * (coalesced) in O(1).
 *
 * Layout of one block:  [ header ][ payload (size bytes) ]
 * The next block starts at (uint8_t*)header + sizeof(header) + size.
 */
struct HeapBlockHeader {
    uint32_t size;                    // payload size in bytes
    uint32_t free;                    // 1 when the block is free
    struct HeapBlockHeader *next;     // next block in address order
    struct HeapBlockHeader *prev;     // previous block in address order
};

/* A block must at least be able to hold its own header. */
#define HEAP_MIN_PAYLOAD  8u
#define HEAP_ALIGN        8u

static uint32_t heap_align_up(uint32_t value) {
    return (value + (HEAP_ALIGN - 1u)) & ~(HEAP_ALIGN - 1u);
}

static uint32_t heap_page_align_up(uint32_t value) {
    return (value + (PAGE_FRAME_SIZE - 1u)) & ~(PAGE_FRAME_SIZE - 1u);
}

/*
 * Map `pages` bytes worth of page frames at [base, base+pages) into the given
 * page directory, then account for the fresh space by either extending the
 * last (free) block or appending a brand new free block at the end of the
 * heap. Returns true on success.
 */
static bool heap_grow(
    struct ProcessControlBlock *pcb,
    struct PageDirectory *page_dir,
    uint32_t need
) {
    uint32_t total = need + sizeof(struct HeapBlockHeader);
    uint32_t pages = heap_page_align_up(total);
    if (pages == 0) {
        pages = PAGE_FRAME_SIZE;
    }

    bool first_grow = (pcb->heap_break == 0);
    uint32_t base = first_grow ? HEAP_BASE : pcb->heap_break;

    if (base < HEAP_BASE || base + pages > HEAP_LIMIT) {
        return false;
    }

    for (uint32_t offset = 0; offset < pages; offset += PAGE_FRAME_SIZE) {
        if (!paging_allocate_user_page_frame(
                page_dir, (void *)(base + offset))) {
            for (uint32_t freed = 0; freed < offset;
                 freed += PAGE_FRAME_SIZE) {
                paging_free_user_page_frame(
                    page_dir, (void *)(base + freed));
            }
            return false;
        }
    }

    pcb->heap_break = base + pages;

    if (first_grow) {
        struct HeapBlockHeader *block = (struct HeapBlockHeader *)HEAP_BASE;
        block->size = pages - sizeof(struct HeapBlockHeader);
        block->free = 1;
        block->next = NULL;
        block->prev = NULL;
        return true;
    }

    struct HeapBlockHeader *last = (struct HeapBlockHeader *)HEAP_BASE;
    while (last->next != NULL) {
        last = last->next;
    }

    if (last->free) {
        // The tail block is free: just absorb the freshly mapped bytes.
        last->size += pages;
    } else {
        struct HeapBlockHeader *block = (struct HeapBlockHeader *)base;
        block->size = pages - sizeof(struct HeapBlockHeader);
        block->free = 1;
        block->next = NULL;
        block->prev = last;
        last->next = block;
    }

    return true;
}

void *heap_malloc(uint32_t size) {
    struct ProcessControlBlock *pcb =
        process_get_current_running_pcb_pointer();
    if (pcb == NULL) {
        return NULL;
    }

    if (size == 0) {
        size = 1;
    }
    size = heap_align_up(size);

    struct PageDirectory *page_dir =
        pcb->context.page_directory_virtual_addr;
    if (page_dir == NULL) {
        return NULL;
    }

    for (;;) {
        struct HeapBlockHeader *block = (struct HeapBlockHeader *)HEAP_BASE;
        bool heap_ready = (pcb->heap_break != 0);

        if (heap_ready) {
            while (block != NULL) {
                if (block->free && block->size >= size) {
                    // Split the block when the remainder can host another block.
                    if (block->size >=
                        size + sizeof(struct HeapBlockHeader) + HEAP_MIN_PAYLOAD) {
                        struct HeapBlockHeader *rest =
                            (struct HeapBlockHeader *)(
                                (uint8_t *)block +
                                sizeof(struct HeapBlockHeader) + size);
                        rest->size = block->size - size -
                                     sizeof(struct HeapBlockHeader);
                        rest->free = 1;
                        rest->next = block->next;
                        rest->prev = block;
                        if (rest->next != NULL) {
                            rest->next->prev = rest;
                        }
                        block->next = rest;
                        block->size = size;
                    }
                    block->free = 0;
                    return (uint8_t *)block + sizeof(struct HeapBlockHeader);
                }
                block = block->next;
            }
        }

        if (!heap_grow(pcb, page_dir, size)) {
            return NULL;
        }
    }
}

bool heap_free(void *ptr) {
    if (ptr == NULL) {
        return true;
    }

    uint32_t address = (uint32_t)ptr;
    if (address < HEAP_BASE + sizeof(struct HeapBlockHeader) ||
        address >= HEAP_LIMIT) {
        return false;
    }

    struct HeapBlockHeader *block =
        (struct HeapBlockHeader *)((uint8_t *)ptr -
                                   sizeof(struct HeapBlockHeader));

    block->free = 1;

    // Coalesce with the following block when it is free.
    if (block->next != NULL && block->next->free) {
        block->size += sizeof(struct HeapBlockHeader) + block->next->size;
        block->next = block->next->next;
        if (block->next != NULL) {
            block->next->prev = block;
        }
    }

    // Coalesce with the preceding block when it is free.
    if (block->prev != NULL && block->prev->free) {
        struct HeapBlockHeader *previous = block->prev;
        previous->size += sizeof(struct HeapBlockHeader) + block->size;
        previous->next = block->next;
        if (block->next != NULL) {
            block->next->prev = previous;
        }
    }

    return true;
}
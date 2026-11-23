#ifndef _HEAP_H
#define _HEAP_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Kernel heap allocator for user processes.
 *
 * Every process owns an independent heap that lives inside its own virtual
 * address space and grows on demand. The region starts at HEAP_BASE (1 GiB)
 * and may grow up to (but not including) HEAP_LIMIT (3 GiB - 1), which keeps
 * it clear of the user program (loaded low, < 1 MiB), the user stack
 * (~3 MiB - 4) and the kernel higher-half (0xC0000000+).
 *
 * Blocks are described by a small header stored inline in the heap memory
 * itself, forming a doubly linked list kept in address order. Allocation uses
 * first-fit; freeing coalesces adjacent free blocks (boundary coalescing).
 * When no existing free block is large enough the heap is grown by mapping
 * additional user page frames.
 */
#define HEAP_BASE   0x40000000u
#define HEAP_LIMIT  0xC0000000u

/**
 * Allocate `size` bytes from the calling process' heap.
 *
 * @param size Requested payload size in bytes (0 is treated as 1).
 * @return Pointer to the allocated payload, or NULL when out of memory or
 *         when called without a running user process.
 */
void *heap_malloc(uint32_t size);

/**
 * Release a block previously returned by heap_malloc().
 *
 * @param ptr Pointer returned by heap_malloc(); NULL is a no-op.
 * @return True on success, false when the pointer is not a valid heap block.
 */
bool heap_free(void *ptr);

#endif
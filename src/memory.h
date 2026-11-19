/* memory.h - Memory Manager interface for kacchiOS
 * Person 1: Core Stack Allocator & Basic Heap Allocator (kmalloc)
 * Person 2: Advanced Heap Deallocator (kfree) & Optimizations
 */

#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

/* Fixed stack parameters matching process.h contract */
#ifndef STACK_SIZE
#define STACK_SIZE       4096
#endif

#ifndef MAX_STACKS
#define MAX_STACKS       32
#endif

/* Total heap capacity reserved for variable-size allocations */
#define HEAP_SIZE        (128 * 1024)   /* 128 KB kernel heap */

/*
 * Public API
 */

/* Initializes the memory subsystem (stacks and heap).
 * Automatically invoked on first allocation if not called explicitly. */
void memory_init(void);

/*
 * Stack Allocation (Fixed 4096-byte blocks, 16-byte aligned)
 * Contract: Returns the LOW address (base pointer) of a free 4096-byte block,
 * or NULL if all 32 process stack slots are in use.
 *
 * Supports both:
 *   - alloc_stack()           [Document 3 / Team Integration standard]
 *   - alloc_stack(STACK_SIZE) [Document 1 / Teacher's Guide standard]
 */
void* alloc_stack(void);
void* alloc_stack_sized(size_t size);

/* Macro wrapper for zero-warning compatibility with 0 or 1 argument */
#if defined(__GNUC__) || defined(__clang__)
#define _ARG2(_0, _1, _2, ...) _2
#define _ALLOC_STACK_CHOOSER(...) _ARG2(__VA_ARGS__, alloc_stack_sized, alloc_stack)
#define alloc_stack(...) _ALLOC_STACK_CHOOSER(dummy __VA_OPT__(,) __VA_ARGS__)(__VA_ARGS__)
#endif

/*
 * Stack Deallocation
 * Reclaims a previously allocated stack block using the same base pointer
 * returned by alloc_stack(). The block is made immediately reusable.
 */
void free_stack(void* ptr);

/*
 * Heap Management (Variable-size blocks)
 * kmalloc: First-fit dynamic memory allocation (Person 1)
 * kfree:   Heap deallocation and block coalescing (Person 2)
 */
void* kmalloc(size_t size);
void  kfree(void* ptr);

/*
 * Diagnostic & Inspection Utilities (Optional / For testing & grading)
 */
size_t memory_get_free_stacks(void);
size_t memory_get_free_heap(void);
void   memory_dump_stats(void);

#endif /* MEMORY_H */

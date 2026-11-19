/* memory.c - Memory Manager implementation for kacchiOS
 *
 * Component Ownership:
 *   - Person 1 (Core):
 *       * memory_init()
 *       * alloc_stack() / alloc_stack_sized()
 *       * free_stack()
 *       * kmalloc()
 *       * Serial debug logging
 *   - Person 2 (Advanced / Extension):
 *       * kfree() expansion (boundary-tag coalescing, leak checks)
 *       * Process termination memory audit
 *       * Bonus: free-list / buddy allocator optimization
 */

#include "memory.h"
#include "serial.h"

/* Undefine compatibility macro so the real function can be defined */
#undef alloc_stack


/* Magic signature for heap block integrity checking ('KACH') */
#define BLOCK_MAGIC 0x4B414348

/*
 * Heap Block Header (16 bytes, preserving 16-byte alignment)
 */
typedef struct block_header {
    size_t size;                /* Size of payload in bytes */
    uint32_t is_free;           /* 1 if block is free, 0 if allocated */
    struct block_header* next;  /* Pointer to next block in heap */
    uint32_t magic;             /* Integrity guard word */
} block_header_t;

/*
 * Static memory arenas:
 * Guaranteed 16-byte aligned and placed in .bss (zero-initialized at boot)
 */
static uint8_t stack_pool[MAX_STACKS][STACK_SIZE] __attribute__((aligned(16)));
static uint8_t stack_used[MAX_STACKS];

static uint8_t heap_pool[HEAP_SIZE] __attribute__((aligned(16)));
static block_header_t* heap_head = NULL;
static int memory_initialized = 0;

/*
 * Internal Debug Formatter Helpers (using serial driver)
 */
static void mem_print_dec(uint32_t n) {
    char buf[12];
    int i = 0;
    if (n == 0) {
        serial_putc('0');
        return;
    }
    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

static void mem_print_hex(uint32_t n) {
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (uint8_t)((n >> i) & 0xF);
        if (nibble < 10) {
            serial_putc((char)('0' + nibble));
        } else {
            serial_putc((char)('a' + (nibble - 10)));
        }
    }
}

/*
 * ============================================================================
 * Memory Manager Initialization
 * ============================================================================
 */
void memory_init(void) {
    if (memory_initialized) {
        return;
    }

    /* Initialize stack slots as free */
    for (int i = 0; i < MAX_STACKS; i++) {
        stack_used[i] = 0;
    }

    /* Initialize heap as one large free block */
    heap_head = (block_header_t*)heap_pool;
    heap_head->size = HEAP_SIZE - sizeof(block_header_t);
    heap_head->is_free = 1;
    heap_head->next = NULL;
    heap_head->magic = BLOCK_MAGIC;

    memory_initialized = 1;

    serial_puts("[MEM] Memory manager initialized: 32 stacks (4KB each), Heap 128KB at ");
    mem_print_hex((uint32_t)heap_pool);
    serial_puts("\n");
}

/*
 * ============================================================================
 * Stack Allocation & Deallocation (Person 1)
 * ============================================================================
 */

/*
 * alloc_stack:
 * Allocates a fixed 4096-byte block for a process stack.
 * Returns the LOW address (base pointer) of the allocated block, 16-byte aligned.
 * Returns NULL if all slots are in use.
 */
void* alloc_stack(void) {
    if (!memory_initialized) {
        memory_init();
    }

    for (int i = 0; i < MAX_STACKS; i++) {
        if (!stack_used[i]) {
            stack_used[i] = 1;
            void* base = (void*)stack_pool[i];

            serial_puts("[MEM] alloc_stack: allocated slot ");
            mem_print_dec((uint32_t)i);
            serial_puts(" at base ");
            mem_print_hex((uint32_t)base);
            serial_puts(" (top ");
            mem_print_hex((uint32_t)base + STACK_SIZE);
            serial_puts(")\n");

            return base;
        }
    }

    serial_puts("[MEM] alloc_stack: FAILED (all 32 stack slots in use)\n");
    return NULL;
}

/* Sized variant to satisfy Document 1 / Teacher's Guide signature */
void* alloc_stack_sized(size_t size) {
    if (size > STACK_SIZE) {
        serial_puts("[MEM] alloc_stack: requested size exceeds 4096 bytes\n");
        return NULL;
    }
    return alloc_stack();
}

/*
 * free_stack:
 * Marks a stack block as free so it can be reused immediately.
 * Expects the same base pointer originally returned by alloc_stack().
 */
void free_stack(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    for (int i = 0; i < MAX_STACKS; i++) {
        if ((void*)stack_pool[i] == ptr) {
            if (!stack_used[i]) {
                serial_puts("[MEM] free_stack: warning, double free detected on slot ");
                mem_print_dec((uint32_t)i);
                serial_puts("\n");
                return;
            }

            stack_used[i] = 0;

            serial_puts("[MEM] free_stack: released slot ");
            mem_print_dec((uint32_t)i);
            serial_puts(" at ");
            mem_print_hex((uint32_t)ptr);
            serial_puts("\n");
            return;
        }
    }

    serial_puts("[MEM] free_stack: warning, pointer not in stack pool ");
    mem_print_hex((uint32_t)ptr);
    serial_puts("\n");
}

/*
 * ============================================================================
 * Heap Allocation (kmalloc) - Person 1
 * ============================================================================
 * First-fit allocation algorithm over the 128 KB heap arena.
 * Every returned payload is guaranteed 16-byte aligned.
 */
void* kmalloc(size_t size) {
    if (!memory_initialized) {
        memory_init();
    }

    if (size == 0) {
        return NULL;
    }

    /* Round up requested size to multiple of 16 bytes for strict alignment */
    size_t aligned_size = (size + 15) & ~((size_t)15);

    block_header_t* curr = heap_head;
    while (curr != NULL) {
        if (curr->magic != BLOCK_MAGIC) {
            serial_puts("[MEM] kmalloc: heap corruption detected! invalid magic\n");
            return NULL;
        }

        if (curr->is_free && curr->size >= aligned_size) {
            /* Check if remaining space is large enough to split into a new block */
            if (curr->size >= aligned_size + sizeof(block_header_t) + 16) {
                block_header_t* new_block = (block_header_t*)((uint8_t*)(curr + 1) + aligned_size);
                new_block->size = curr->size - aligned_size - sizeof(block_header_t);
                new_block->is_free = 1;
                new_block->next = curr->next;
                new_block->magic = BLOCK_MAGIC;

                curr->size = aligned_size;
                curr->next = new_block;
            }

            curr->is_free = 0;
            void* payload = (void*)(curr + 1);

            serial_puts("[MEM] kmalloc: allocated ");
            mem_print_dec((uint32_t)size);
            serial_puts(" bytes at ");
            mem_print_hex((uint32_t)payload);
            serial_puts("\n");

            return payload;
        }
        curr = curr->next;
    }

    serial_puts("[MEM] kmalloc: FAILED for ");
    mem_print_dec((uint32_t)size);
    serial_puts(" bytes (out of memory)\n");
    return NULL;
}

/*
 * ============================================================================
 * Heap Deallocation (kfree) - Base implementation by Person 1 / Owned by Person 2
 * ============================================================================
 * Person 2 can extend this with:
 *   - Bidirectional / boundary-tag coalescing
 *   - Process leak verification on termination
 *   - Free-list or buddy system bonus optimization
 */
void kfree(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    /* Heap bounds validation */
    if ((uint8_t*)ptr < heap_pool || (uint8_t*)ptr >= heap_pool + HEAP_SIZE) {
        serial_puts("[MEM] kfree: pointer outside heap bounds ");
        mem_print_hex((uint32_t)ptr);
        serial_puts("\n");
        return;
    }

    block_header_t* header = (block_header_t*)ptr - 1;

    if (header->magic != BLOCK_MAGIC) {
        serial_puts("[MEM] kfree: corrupted header magic at ");
        mem_print_hex((uint32_t)header);
        serial_puts("\n");
        return;
    }

    if (header->is_free) {
        serial_puts("[MEM] kfree: warning, double free detected at ");
        mem_print_hex((uint32_t)ptr);
        serial_puts("\n");
        return;
    }

    header->is_free = 1;

    serial_puts("[MEM] kfree: freed ");
    mem_print_dec((uint32_t)header->size);
    serial_puts(" bytes at ");
    mem_print_hex((uint32_t)ptr);
    serial_puts("\n");

    /*
     * Baseline coalescing: merges consecutive free blocks to combat fragmentation.
     * Person 2: Keep or optimize this routine!
     */
    block_header_t* curr = heap_head;
    while (curr != NULL && curr->next != NULL) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

/*
 * ============================================================================
 * Diagnostic / Inspection Utilities
 * ============================================================================
 */
size_t memory_get_free_stacks(void) {
    size_t count = 0;
    for (int i = 0; i < MAX_STACKS; i++) {
        if (!stack_used[i]) {
            count++;
        }
    }
    return count;
}

size_t memory_get_free_heap(void) {
    size_t total = 0;
    block_header_t* curr = heap_head;
    while (curr != NULL) {
        if (curr->is_free) {
            total += curr->size;
        }
        curr = curr->next;
    }
    return total;
}

void memory_dump_stats(void) {
    serial_puts("[MEM] === Memory Stats ===\n");
    serial_puts("[MEM] Free stack slots: ");
    mem_print_dec((uint32_t)memory_get_free_stacks());
    serial_puts(" / ");
    mem_print_dec(MAX_STACKS);
    serial_puts("\n[MEM] Total free heap: ");
    mem_print_dec((uint32_t)memory_get_free_heap());
    serial_puts(" bytes\n");
}

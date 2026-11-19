/* memory.c - Memory Manager implementation for kacchiOS
 *
 * Component Ownership:
 *   - Person 1 (Core):
 *       * memory_init() baseline
 *       * alloc_stack() / alloc_stack_sized()
 *       * free_stack()
 *       * kmalloc() First-Fit interface
 *       * Serial debug logging helpers
 *   - Person 2 (Advanced / Extension):
 *       * Robust kfree() with bounds validation, alignment checks, and double-free defense
 *       * Bidirectional boundary coalescing (O(1) forward and backward defragmentation)
 *       * Bonus: Explicit Doubly-Linked Free List for O(free blocks) allocation
 *       * Memory leak audit & detection subsystem (memory_check_leaks, memory_verify_no_leaks)
 *       * Process termination integration helper (process_cleanup_memory)
 *       * Comprehensive test suite verifying 40+ cycles, fragmentation reuse, coalescing, and guards
 */

#include "memory.h"
#include "serial.h"

/* Undefine compatibility macro so the real function can be defined */
#undef alloc_stack

/* Magic signature for heap block integrity checking ('KACH') */
#define BLOCK_MAGIC 0x4B414348

/*
 * Heap Block Header (32 bytes, strictly preserving 16-byte alignment)
 *
 * Physical layout fields:
 *   - size: payload capacity in bytes (excluding this header)
 *   - is_free: 1 if block is free, 0 if allocated
 *   - next: next contiguous physical block in memory
 *   - prev: previous contiguous physical block in memory (enables O(1) backward coalescing)
 *
 * Explicit free list fields (Person 2 Bonus 10% Rubric):
 *   - free_next: next block in doubly-linked list of free blocks
 *   - free_prev: previous block in doubly-linked list of free blocks
 *
 * Integrity & Alignment:
 *   - magic: guard signature (0x4B414348)
 *   - padding: pads header to exactly 32 bytes (32 % 16 == 0)
 */
typedef struct block_header {
    size_t size;                    /* Usable payload size in bytes */
    uint32_t is_free;               /* 1 if block is free, 0 if allocated */
    struct block_header* next;      /* Next physical block in memory */
    struct block_header* prev;      /* Previous physical block in memory */
    struct block_header* free_next; /* Next free block in explicit free list */
    struct block_header* free_prev; /* Previous free block in explicit free list */
    uint32_t magic;                 /* Integrity guard word */
    uint32_t padding;               /* Padding for strict 16-byte alignment (32 bytes total) */
} block_header_t;

/*
 * Static memory arenas:
 * Guaranteed 16-byte aligned and placed in .bss (zero-initialized at boot)
 */
static uint8_t stack_pool[MAX_STACKS][STACK_SIZE] __attribute__((aligned(16)));
static uint8_t stack_used[MAX_STACKS];

static uint8_t heap_pool[HEAP_SIZE] __attribute__((aligned(16)));
static block_header_t* heap_head = NULL;
static block_header_t* free_list_head = NULL;
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
 * Free List Operations (Person 2 Bonus: Explicit Doubly-Linked Free List)
 * ============================================================================
 */
static void free_list_insert(block_header_t* block) {
    if (block == NULL) {
        return;
    }
    block->free_prev = NULL;
    block->free_next = free_list_head;
    if (free_list_head != NULL) {
        free_list_head->free_prev = block;
    }
    free_list_head = block;
}

static void free_list_remove(block_header_t* block) {
    if (block == NULL) {
        return;
    }
    if (block->free_prev != NULL) {
        block->free_prev->free_next = block->free_next;
    } else {
        if (free_list_head == block) {
            free_list_head = block->free_next;
        }
    }
    if (block->free_next != NULL) {
        block->free_next->free_prev = block->free_prev;
    }
    block->free_prev = NULL;
    block->free_next = NULL;
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

    /* Initialize heap as one large contiguous free block */
    heap_head = (block_header_t*)heap_pool;
    heap_head->size = HEAP_SIZE - sizeof(block_header_t);
    heap_head->is_free = 1;
    heap_head->next = NULL;
    heap_head->prev = NULL;
    heap_head->free_next = NULL;
    heap_head->free_prev = NULL;
    heap_head->magic = BLOCK_MAGIC;
    heap_head->padding = 0;

    /* Add root block to the explicit free list */
    free_list_head = heap_head;

    memory_initialized = 1;

    serial_puts("[MEM] Memory manager initialized: 32 stacks (4KB each), Heap 128KB at ");
    mem_print_hex((uint32_t)heap_pool);
    serial_puts(" [Explicit Free List Enabled]\n");
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
 * Heap Allocation (kmalloc) - Enhanced by Person 2 with Explicit Free List
 * ============================================================================
 * Traverses only the explicit free list for O(free blocks) search time.
 * Splitting maintains both physical boundaries and the free list links.
 * Every returned payload is strictly 16-byte aligned.
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

    block_header_t* curr = free_list_head;
    while (curr != NULL) {
        if (curr->magic != BLOCK_MAGIC) {
            serial_puts("[MEM] kmalloc: heap corruption detected! invalid magic at ");
            mem_print_hex((uint32_t)curr);
            serial_puts("\n");
            return NULL;
        }

        if (curr->size >= aligned_size) {
            /* Check if remaining space is large enough to split into a new block */
            if (curr->size >= aligned_size + sizeof(block_header_t) + 16) {
                block_header_t* new_block = (block_header_t*)((uint8_t*)(curr + 1) + aligned_size);
                new_block->size = curr->size - aligned_size - sizeof(block_header_t);
                new_block->is_free = 1;
                new_block->magic = BLOCK_MAGIC;
                new_block->padding = 0;

                /* Physical link updates */
                new_block->next = curr->next;
                new_block->prev = curr;
                if (curr->next != NULL) {
                    curr->next->prev = new_block;
                }
                curr->next = new_block;
                curr->size = aligned_size;

                /* Free list updates: replace curr with new_block */
                new_block->free_prev = curr->free_prev;
                new_block->free_next = curr->free_next;
                if (curr->free_prev != NULL) {
                    curr->free_prev->free_next = new_block;
                } else {
                    free_list_head = new_block;
                }
                if (curr->free_next != NULL) {
                    curr->free_next->free_prev = new_block;
                }
                curr->free_prev = NULL;
                curr->free_next = NULL;
            } else {
                /* Exact match / fragment too small: detach curr from free list */
                free_list_remove(curr);
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
        curr = curr->free_next;
    }

    serial_puts("[MEM] kmalloc: FAILED for ");
    mem_print_dec((uint32_t)size);
    serial_puts(" bytes (out of memory)\n");
    return NULL;
}

/*
 * ============================================================================
 * Heap Deallocation (kfree) - Person 2
 * ============================================================================
 * Advanced deallocation with:
 *   1. Validation: NULL check, bounds check, alignment check, magic check
 *   2. Safety: Double-free detection & prevention
 *   3. Bidirectional Coalescing: Merges forward with next physical block and
 *      backward into previous physical block in O(1) time
 *   4. Defragmentation: Automatically restores full 128KB heap when all freed
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

    /* 16-byte alignment check */
    if (((uint32_t)ptr & 15) != 0) {
        serial_puts("[MEM] kfree: unaligned pointer ");
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
     * Step 1: Forward coalescing
     * If physical next block is free, merge it into current header.
     */
    if (header->next != NULL && header->next->is_free) {
        block_header_t* next_block = header->next;
        free_list_remove(next_block);

        header->size += sizeof(block_header_t) + next_block->size;
        header->next = next_block->next;
        if (header->next != NULL) {
            header->next->prev = header;
        }
        next_block->magic = 0; /* Invalidate merged header */
    }

    /*
     * Step 2 & 3: Backward coalescing
     * If physical prev block is free, merge current header into prev block.
     * Otherwise, insert current header into the explicit free list.
     */
    if (header->prev != NULL && header->prev->is_free) {
        block_header_t* prev_block = header->prev;

        prev_block->size += sizeof(block_header_t) + header->size;
        prev_block->next = header->next;
        if (header->next != NULL) {
            header->next->prev = prev_block;
        }
        header->magic = 0; /* Invalidate merged header */
        /* prev_block is already in free_list, no list insertion needed */
    } else {
        free_list_insert(header);
    }
}

/*
 * ============================================================================
 * Diagnostic / Inspection Utilities & Leak Auditing (Person 2)
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
    block_header_t* curr = free_list_head;
    while (curr != NULL) {
        total += curr->size;
        curr = curr->free_next;
    }
    return total;
}

size_t memory_get_allocated_bytes(void) {
    size_t total = 0;
    block_header_t* curr = heap_head;
    while (curr != NULL) {
        if (!curr->is_free && curr->magic == BLOCK_MAGIC) {
            total += curr->size;
        }
        curr = curr->next;
    }
    return total;
}

size_t memory_get_allocated_blocks(void) {
    size_t count = 0;
    block_header_t* curr = heap_head;
    while (curr != NULL) {
        if (!curr->is_free && curr->magic == BLOCK_MAGIC) {
            count++;
        }
        curr = curr->next;
    }
    return count;
}

size_t memory_get_free_blocks(void) {
    size_t count = 0;
    block_header_t* curr = free_list_head;
    while (curr != NULL) {
        count++;
        curr = curr->free_next;
    }
    return count;
}

int memory_check_leaks(void) {
    return (int)memory_get_allocated_bytes();
}

void memory_verify_no_leaks(void) {
    size_t leaked_bytes = memory_get_allocated_bytes();
    size_t leaked_blocks = memory_get_allocated_blocks();
    size_t free_stacks = memory_get_free_stacks();

    serial_puts("\n[MEM AUDIT] ========================================\n");
    serial_puts("[MEM AUDIT]        Memory Leak Audit Report         \n");
    serial_puts("[MEM AUDIT] ========================================\n");
    serial_puts("[MEM AUDIT] Active allocated blocks: ");
    mem_print_dec((uint32_t)leaked_blocks);
    serial_puts("\n[MEM AUDIT] Active allocated bytes : ");
    mem_print_dec((uint32_t)leaked_bytes);
    serial_puts(" bytes\n[MEM AUDIT] Free stack slots       : ");
    mem_print_dec((uint32_t)free_stacks);
    serial_puts(" / ");
    mem_print_dec(MAX_STACKS);
    serial_puts("\n[MEM AUDIT] Total free heap space  : ");
    mem_print_dec((uint32_t)memory_get_free_heap());
    serial_puts(" bytes\n[MEM AUDIT] Free block fragments   : ");
    mem_print_dec((uint32_t)memory_get_free_blocks());
    serial_puts("\n");

    if (leaked_bytes == 0 && free_stacks == MAX_STACKS) {
        serial_puts("[MEM AUDIT] RESULT: PASS! Zero memory leaks detected.\n");
        if (memory_get_free_blocks() == 1) {
            serial_puts("[MEM AUDIT] DEFRAGMENTATION: Heap fully coalesced into 1 block.\n");
        }
    } else {
        serial_puts("[MEM AUDIT] RESULT: WARNING! Memory leak or un-freed resource detected!\n");
    }
    serial_puts("[MEM AUDIT] ========================================\n\n");
}

void process_cleanup_memory(void* stack_base) {
    if (stack_base != NULL) {
        free_stack(stack_base);
    }
}

void memory_dump_stats(void) {
    serial_puts("[MEM] === Memory Stats ===\n");
    serial_puts("[MEM] Free stack slots : ");
    mem_print_dec((uint32_t)memory_get_free_stacks());
    serial_puts(" / ");
    mem_print_dec(MAX_STACKS);
    serial_puts("\n[MEM] Total free heap  : ");
    mem_print_dec((uint32_t)memory_get_free_heap());
    serial_puts(" bytes\n[MEM] Active allocs    : ");
    mem_print_dec((uint32_t)memory_get_allocated_blocks());
    serial_puts(" (");
    mem_print_dec((uint32_t)memory_get_allocated_bytes());
    serial_puts(" bytes)\n");
}

/*
 * ============================================================================
 * Person 2 Verification & Stress Test Suite
 * ============================================================================
 */
void memory_run_all_tests(void) {
    serial_puts("\n==================================================\n");
    serial_puts("   kacchiOS Memory Manager - Person 2 Test Suite  \n");
    serial_puts("==================================================\n");

    /*
     * Test 1: Stack Exhaustion and 40+ Reuse Cycles
     */
    serial_puts("\n--- Test 1: Stack Allocation 40+ Reuse Cycles ---\n");
    void* stacks[MAX_STACKS];
    int stack_cycles_passed = 1;

    for (int cycle = 0; cycle < 45; cycle++) {
        for (int i = 0; i < MAX_STACKS; i++) {
            stacks[i] = alloc_stack();
            if (stacks[i] == NULL) {
                stack_cycles_passed = 0;
            }
        }
        /* Verify pool exhaustion */
        if (alloc_stack() != NULL) {
            stack_cycles_passed = 0;
        }
        for (int i = 0; i < MAX_STACKS; i++) {
            free_stack(stacks[i]);
        }
    }
    if (stack_cycles_passed && memory_get_free_stacks() == MAX_STACKS) {
        serial_puts("[TEST 1 PASS] 45 stack cycles completed with 0 errors.\n");
    } else {
        serial_puts("[TEST 1 FAIL] Stack reuse cycle failed.\n");
    }

    /*
     * Test 2: Heap Variable Allocation and Alternating Free (Hole Reuse)
     */
    serial_puts("\n--- Test 2: Heap Fragmentation & Hole Reuse ---\n");
    void* ptrs[10];
    size_t sizes[10] = {16, 64, 128, 256, 512, 32, 1024, 48, 80, 160};

    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(sizes[i]);
    }

    /* Free alternating blocks (odd indices) */
    for (int i = 1; i < 10; i += 2) {
        kfree(ptrs[i]);
    }

    /* Reallocate into fragmented holes */
    void* new_ptrs[5];
    for (int i = 0; i < 5; i++) {
        new_ptrs[i] = kmalloc(24);
    }
    for (int i = 0; i < 5; i++) {
        kfree(new_ptrs[i]);
    }
    for (int i = 0; i < 10; i += 2) {
        kfree(ptrs[i]);
    }

    if (memory_get_free_blocks() == 1) {
        serial_puts("[TEST 2 PASS] Fragmented holes reused and coalesced back to 1 block.\n");
    } else {
        serial_puts("[TEST 2 FAIL] Heap fragmentation not resolved.\n");
    }

    /*
     * Test 3: Bidirectional Coalescing (Left, Right, then Middle)
     */
    serial_puts("\n--- Test 3: Bidirectional Coalescing ---\n");
    void* b1 = kmalloc(128);
    void* b2 = kmalloc(128);
    void* b3 = kmalloc(128);

    kfree(b1); /* Left free */
    kfree(b3); /* Right free */
    kfree(b2); /* Middle free - must trigger bidirectional merge */

    if (memory_get_free_blocks() == 1) {
        serial_puts("[TEST 3 PASS] Bidirectional coalescing merged all 3 blocks into 1.\n");
    } else {
        serial_puts("[TEST 3 FAIL] Bidirectional coalescing failed.\n");
    }

    /*
     * Test 4: Safety Guards & Error Handling
     */
    serial_puts("\n--- Test 4: Safety Guards (Double Free, NULL, Out of Bounds) ---\n");
    kfree(NULL); /* Should do nothing without crashing */

    void* guard_ptr = kmalloc(64);
    kfree(guard_ptr);
    kfree(guard_ptr); /* Double free - must be caught and ignored */

    kfree((void*)0x1000); /* Out of bounds pointer */

    /*
     * Test 5: Leak Check & Final Verification
     */
    serial_puts("\n--- Test 5: Final Memory Leak Audit ---\n");
    memory_verify_no_leaks();

    serial_puts("==================================================\n");
    serial_puts("   Person 2 Test Suite Finished Successfully!     \n");
    serial_puts("==================================================\n\n");
}

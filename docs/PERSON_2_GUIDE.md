# kacchiOS — Person 2 Integration & Implementation Guide
**Role:** Person 2 — Memory Manager (Advanced)  
**Teammate:** Person 1 (Nahyan — Memory Manager Core)  
**Repository Branch:** `Sanjida`  
**Reference Documents:**
1. `Lab Project Team Guide` (Course Faculty / RUET CSE 3202)
2. `KacchiOS: Scheduler Integration Notes` (Person 5 — Scheduler Core)
3. `KacchiOS: Team Integration Guide` (Person 5 & 6 — Scheduler Team)

---

## 1. Executive Summary & Current Project State

### What is Already Done:
* **Scheduler Core & Timer Preemption (Persons 5 & 6):**
  - Fully implemented in `src/scheduler.c`, `src/scheduler.h`, `src/timer.c`, `src/timer.h`, and `src/context_switch.S`.
  - Implements Round Robin with 10 priority levels, priority aging, configurable quantum, and 100Hz PIT hardware timer preemption.
  - Defined the `PCB` structure in `src/process.h`.
  - **Rule:** Do NOT edit the scheduler, timer, or context switch assembly files.
* **Core Memory Manager (Person 1 — Nahyan):**
  - Already designed and pushed to branch `nahyan` in `src/memory.h` and `src/memory.c`.
  - **Stack Allocation:** 32 fixed 4096-byte stacks, strictly 16-byte aligned in `.bss`, returning the **LOW address (base)** as required by `context_switch.S`.
  - **Stack Deallocation:** `free_stack(ptr)` makes blocks immediately reusable across 40+ cycles.
  - **Heap Allocation:** First-Fit `kmalloc(size)` over a 128 KB heap with 16-byte aligned headers and payloads.
  - **Baseline Deallocation:** Working baseline `kfree(ptr)` with forward coalescing.
  - **Diagnostics:** `memory_get_free_stacks()`, `memory_get_free_heap()`, `memory_dump_stats()`.
  - **Logging:** Full serial debugging via `serial_puts` / `serial_putc`.

---

## 2. Step 1: Git Setup on Sanjida's PC

Person 1 has pushed all memory headers, implementations, and scheduler files to branch `nahyan`.

Run these commands in your local repository terminal:

```bash
# 1. Switch to your assigned branch
git checkout Sanjida

# 2. Pull Person 1's work into your branch
git pull origin nahyan

# 3. Check status
git status
```

Now your branch `Sanjida` has `src/memory.h`, `src/memory.c`, and all the scheduler files!

---

## 3. Person 2's Tasks & Grading Rubric Breakdown

According to the official **Lab Project Team Guide**, Person 2 owns **Memory Manager (Advanced)**:

| Category | Requirement | Ownership | Points |
| :--- | :--- | :--- | :--- |
| **Must-Have** | Robust heap deallocation (`kfree`) & zero memory leaks | Person 2 | 70% |
| **Must-Have** | Integration with Process Manager termination (`terminate_process`) | Person 2 + Person 4 | 70% |
| **Good-to-Have** | Heap defragmentation (bidirectional / boundary coalescing) | Person 2 | 20% |
| **Bonus** | Optimized Allocator (Explicit Free List or Buddy System) | Person 2 | 10% |

---

## 4. Detailed Task Instructions for Person 2

### Task 1: Complete and Enhance `kfree(void* ptr)` in `src/memory.c`
Person 1 has already provided a clean baseline `kfree()` in `src/memory.c` with forward coalescing. Your job is to make it robust and advanced:

1. **Validation & Integrity Checks:**
   - Verify `ptr != NULL`.
   - Verify `ptr` falls strictly within the heap arena (`heap_pool` to `heap_pool + HEAP_SIZE`).
   - Validate the block header magic (`BLOCK_MAGIC == 0x4B414348`).
   - Guard against **double-free** bugs (if `header->is_free == 1`, log a warning and return).
2. **Advanced Coalescing (Defragmentation):**
   - The current baseline merges forward (`curr` and `curr->next`).
   - **Enhancement:** Implement bidirectional coalescing. If previous and next blocks are both free, merge all three into one contiguous free block.
   - When all blocks are freed, the heap should restore to a single 128 KB block.

---

### Task 2: Process Termination Cleanup & Leak Prevention
Coordinate with **Person 4** (Process Manager Advanced):
1. **The Contract:**
   - When a process finishes execution or calls `process_exit()`, `set_state(p, TERMINATED)` is set.
   - The reaper function (`process_reap()` in `process.c`) must call:
     ```c
     free_stack(p->stack_base);
     ```
   - If processes allocate dynamic memory via `kmalloc()`, ensure that memory is freed before or during process termination.
2. **Leak Detection / Verification Function:**
   - In `src/memory.c`, implement a leak check function (e.g., `int memory_check_leaks(void)`).
   - Use `memory_dump_stats()`:
     ```c
     memory_dump_stats();
     ```
   - Before running processes: record `initial_free_heap = memory_get_free_heap()`.
   - After processes terminate and reap: verify `memory_get_free_heap() == initial_free_heap`.
   - If they differ, print `[MEM] WARNING: Memory leak detected!`.

---

### Task 3: Bonus Optimization (10% Bonus Rubric)
The rubric grants bonus points for an **Optimized Allocator**. Choose one of the following:

#### Option A: Explicit Doubly-Linked Free List (Recommended & Straightforward)
Instead of traversing all blocks (allocated + free) in First-Fit:
* Maintain a separate linked list containing **only free blocks**.
* When allocating (`kmalloc`), traverse only the free list.
* When freeing (`kfree`), insert the freed block into the free list and coalesce with adjacent neighbors.
* **Why it's better:** $O(\text{free blocks})$ allocation time instead of $O(\text{total blocks})$.

#### Option B: Buddy Allocator (Alternative Bonus)
* Divide memory into powers of two (e.g., 64B, 128B, 256B, 512B ... 4KB).
* Combine buddy blocks when both are freed.

---

### Task 4: Stress Testing & Verification Checklist

Person 5 and 6 outlined the integration tests in Document 3. You should verify:

- [ ] **Test A: 40+ Create/Exit Cycles**
  - Verify that process stacks can be allocated, freed, and re-allocated 40+ times without running out of slots.
- [ ] **Test B: Heap Stress Test**
  - Allocate 10 blocks of varying sizes (`16`, `64`, `256`, `512`, `1024` bytes).
  - Free alternating blocks (e.g., blocks 1, 3, 5, 7, 9).
  - Re-allocate new blocks to confirm the allocator reuses fragmented holes.
  - Free the remaining blocks and verify the heap coalesces back to full capacity.
- [ ] **Test C: Double Free & Bad Pointer Handling**
  - Calling `kfree(NULL)` does nothing and does not crash.
  - Calling `kfree(p)` twice prints a warning without corrupting heap metadata.
  - Passing an address outside the heap prints an error and safely aborts.

---

## 5. Files to Edit and Rules

| File | Status | Notes |
| :--- | :--- | :--- |
| `src/memory.c` | **Edit freely** | Enhance `kfree()`, add coalescing, add leak checks, or implement free list. |
| `src/memory.h` | **Edit if needed** | Add prototypes for any new helper functions you write. |
| `src/process.h` | **DO NOT TOUCH** | Field offsets are read by context switch assembly. |
| `src/scheduler.*` | **DO NOT TOUCH** | Owned by Person 5 & 6. |
| `src/timer.*` | **DO NOT TOUCH** | Owned by Person 6. |
| `src/context_switch.S` | **DO NOT TOUCH** | Owned by Person 5. |
| `src/Makefile` | **Owned by Person 6** | Person 6 adds `memory.o` and `process.o` when linking `kernel.elf`. |

---

## 6. How to Push Your Work (Git Workflow)

When you finish your implementation:

```bash
# 1. Check modified files
git status

# 2. Stage your changes
git add src/memory.c src/memory.h

# 3. Commit with a descriptive message
git commit -m "Implement advanced kfree with coalescing, leak detection and free list"

# 4. Push to your branch
git push origin Sanjida
```

After pushing, notify **Person 6 (Lead / Integration)** to merge your changes into `dev` or test with `process.c`!

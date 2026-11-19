#ifndef SCHEDULER_H
#define SCHEDULER_H

typedef struct PCB PCB;

void scheduler_init(void);
void schedule(void);
void context_switch(PCB* old, PCB* new);
int scheduler_ready_count(void);

/* Returns 1 on success and 0 on failure */

int scheduler_add(PCB* p);
void scheduler_set_idle(PCB* p);
void scheduler_tick(void);                  // called once per timer tick 
void scheduler_yield(void);                 // interrupt-safe voluntary yield 
int scheduler_set_quantum(int ticks);       // 1 ok, 0 if ticks < 1 
int scheduler_get_quantum(void);
void scheduler_set_aging(int enabled);
int scheduler_set_priority(PCB* p, int priority);
void scheduler_enable_preemption(void);
int scheduler_preemptive(void);

static inline unsigned int irq_save(void)
{
	unsigned int f;
	__asm__ volatile("pushfl; popl %0; cli" : "=r"(f) : : "memory"); 
	return f;
}

static inline void irq_restore(unsigned int f)
{
	__asm__ volatile("pushl %0; popfl" : : "r"(f) : "memory", "cc");
}


#endif

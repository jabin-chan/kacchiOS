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

#endif

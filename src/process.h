#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#define CURRENT 0
#define READY 1
#define TERMINATED 2

typedef struct PCB
{
	int32_t pid;
	int32_t state;
	uint32_t stack_ptr;
	void (*entry)(void);
} PCB;

PCB* get_current_process(void);
void set_state(PCB* p, int32_t state);
void set_current_process(PCB* p);

#endif


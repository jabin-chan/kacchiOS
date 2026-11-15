#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#define CURRENT 0
#define READY 1
#define TERMINATED 2
#define UNUSED           3
#define MAX_PROCESSES    32
#define STACK_SIZE       4096
#define DEFAULT_PRIORITY 1
#define MAX_PRIORITY     9 
typedef struct PCB
{
	int32_t pid;
	int32_t state;
	uint32_t stack_ptr;
	void (*entry)(void);
	void* stack_base;
	int32_t priority;       //current one and aging can raise it 
	int32_t base_priority;  //what it goes back to after running 
	int32_t wait_ticks;     //how many ticks are spent waiting in the ready queue

} PCB;

PCB* get_current_process(void);
void set_state(PCB* p, int32_t state);
void set_current_process(PCB* p);

#endif


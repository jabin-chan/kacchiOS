#include "process.h"
#include "scheduler.h"
#include "serial.h"
#include "types.h"
#define MAX_READY_QUEUE 32

static void print_int(int n)
{
	char buffer[12];
	int i = 0;

	if (n == 0)
	{
		serial_putc('0');
		return;
	}

	if (n < 0)
	{
		serial_putc('-');
		n = -n;
	}

	while (n > 0)
	{
		buffer[i++] = '0' + (n % 10);
		n /= 10;
	}

	while (i > 0)
	{
		serial_putc(buffer[--i]);
	}
}

static PCB* ready_queue[MAX_READY_QUEUE];
static int front = 0;
static int rear = 0;
static int count = 0;
static PCB* idle_process = NULL;
static void sort_queue(void);

static int enqueue(PCB* p)
{
	if (p == NULL)
	{
		return 0;
	}

	if (count < MAX_READY_QUEUE)
	{
		ready_queue[rear] = p;
		rear = (rear + 1) % MAX_READY_QUEUE;
		count++;
		return 1;
	}

	return 0;
}


static PCB* dequeue(void)
{
	if (count == 0)
	{
		return NULL;

	}

	PCB* p;
	p = ready_queue[front];
	front = (front + 1) % MAX_READY_QUEUE;
	count--;

	return p;
}


void scheduler_set_idle(PCB* p)
{
	idle_process = p;
}


static PCB* pick_next(void)
{
	PCB* p;

	while (count > 0)
	{
		p = dequeue();
		
		if (p != NULL && p->state == READY && p->stack_ptr != 0)
		{
			return p;
		}
	}
	return NULL;
}

void scheduler_init(void)
{
	front = 0;
	rear = 0;
	count = 0;
}


void schedule(void)
{
	sort_queue();
	PCB* old_process = get_current_process();
	PCB* next_process = pick_next();
	
	if (next_process == NULL)
	{
		if (old_process != NULL && old_process->state == CURRENT)
		{
			return;
		}

		if (idle_process == NULL || idle_process == old_process || idle_process->stack_ptr == 0)
		{
			return;
		}
		next_process = idle_process;
	}

	if (old_process != NULL && old_process->state == CURRENT && old_process != idle_process)
	{
		enqueue(old_process);
		set_state(old_process, READY);
	}

	else if (old_process != NULL && old_process == idle_process && old_process->state == CURRENT)
	{
		set_state(old_process, READY);
	}
	
	set_state(next_process, CURRENT);
	set_current_process(next_process);
	serial_puts("Scheduler: Switching from ");
	
	if (old_process == NULL)
	{
		serial_puts("none");
	}
	else
	{
		print_int(old_process->pid);
	}
	serial_puts(" to ");

	if(next_process == NULL)
	{
		serial_puts("none");
	}

	else
	{
		print_int(next_process->pid);
	}
	serial_puts("\n");
    context_switch(old_process, next_process);
}


int scheduler_ready_count(void)
{
	return count;
}

/*Adds process to the ready queue, set_state() shouldn't queue it*/
int scheduler_add(PCB* p)
{
	if (p == NULL)
	{
		return 0;
	}
	
	if (p->stack_ptr == 0)
	{
		return 0;
	}

	if (p->state == TERMINATED || p->state == CURRENT)
	{
		return 0;
	}

	for (int i = 0, index = front; i < count; i++)
	{
		if (ready_queue[index] == p)
		{
			return 0;
		}
		index = (index + 1) % MAX_READY_QUEUE;
	}

	if (count >= MAX_READY_QUEUE)
	{
		return 0;
	}

	if (!enqueue(p))
	{
		return 0;
	}

	set_state(p, READY);
	return 1;
}

#ifndef MAX_PRIORITY
#define MAX_PRIORITY 9
#endif
#define DEFAULT_QUANTUM 3      //ticks 
#define AGING_THRESHOLD 10     // ticks waiting before priority +1 

static int time_quantum = DEFAULT_QUANTUM;
static int ticks_left = DEFAULT_QUANTUM;
static int aging_enabled = 1;
static int preemptive = 0;

//stable insertion sort: highest priority first, ties keep FIFO order 
static void sort_queue(void)
{
	for (int i = 1; i < count; i++)
	{
		PCB* key = ready_queue[(front + i) % MAX_READY_QUEUE];
		int j = i - 1;

		while (j >= 0 && ready_queue[(front + j) % MAX_READY_QUEUE]->priority < key->priority)
		{
			ready_queue[(front + j + 1) % MAX_READY_QUEUE] = ready_queue[(front + j) % MAX_READY_QUEUE];
			j--;
		}
		ready_queue[(front + j + 1) % MAX_READY_QUEUE] = key;
	}
}

//highest priority among valid waiting processes, -1 if none 
static int top_waiting_priority(void)
{
	int best = -1;

	for (int i = 0; i < count; i++)
	{
		PCB* p = ready_queue[(front + i) % MAX_READY_QUEUE];

		if (p != NULL && p->state == READY && p->stack_ptr != 0 && p->priority > best)
		{
			best = p->priority;
		}
	}
	return best;
}

static void age_waiting_processes(void)
{
	if (!aging_enabled)
	{
		return;
	}

	for (int i = 0; i < count; i++)
	{
		PCB* p = ready_queue[(front + i) % MAX_READY_QUEUE];

		if (p == NULL || p->state != READY)
		{
			continue;
		}

		p->wait_ticks++;

		if (p->wait_ticks >= AGING_THRESHOLD)
		{
			p->wait_ticks = 0;

			if (p->priority < MAX_PRIORITY)
			{
				p->priority++;
				serial_puts("Scheduler: aging pid ");
				print_int(p->pid);
				serial_puts(" -> priority ");
				print_int(p->priority);
				serial_puts("\n");
			}
		}
	}
}

// running process leaves the CPU: drop any aging boost 
static void reset_priority(PCB* p)
{
	if (p != NULL && p != idle_process)
	{
		p->priority = p->base_priority;
		p->wait_ticks = 0;
	}
}
//called once per timer tick, from the timer ISR (interrupts already off) 
void scheduler_tick(void)
{
	PCB* cur = get_current_process();

	age_waiting_processes();

	if (ticks_left > 0)
	{
		ticks_left--;
	}
	if (ticks_left > 0)
	{
		return;
	}

	//quantum used up 
	ticks_left = time_quantum;

	// keep running if strictly higher priority than everyone waiting.
	if (cur != NULL && cur != idle_process && cur->state == CURRENT &&
	    cur->priority > top_waiting_priority())
	{
		return;
	}

	reset_priority(cur);
	sort_queue();
	schedule();
}

void scheduler_yield(void)
{
	unsigned int flags = irq_save();

	reset_priority(get_current_process());
	ticks_left = time_quantum;
	sort_queue();
	schedule();

	irq_restore(flags);   // runs when this context is switched back in 
}

int scheduler_set_quantum(int ticks)
{
	if (ticks < 1)
	{
		return 0;
	}

	unsigned int flags = irq_save();
	time_quantum = ticks;
	ticks_left = ticks;
	irq_restore(flags);
	return 1;
}

int scheduler_get_quantum(void)
{
	return time_quantum;
}

void scheduler_enable_preemption(void)
{
	preemptive = 1;
}

int scheduler_preemptive(void)
{
	return preemptive;
}

void scheduler_set_aging(int enabled)
{
	aging_enabled = enabled;
}

int scheduler_set_priority(PCB* p, int priority)
{
	if (p == NULL)
	{
		return 0;
	}

	if (priority < 0)
	{
		priority = 0;
	}
	if (priority > MAX_PRIORITY)
	{
		priority = MAX_PRIORITY;
	}

	unsigned int flags = irq_save();
	p->base_priority = priority;
	p->priority = priority;
	irq_restore(flags);
	return 1;
}

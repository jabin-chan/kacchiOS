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


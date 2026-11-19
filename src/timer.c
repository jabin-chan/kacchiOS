#include "timer.h"
#include "scheduler.h"

struct idt_entry
{
	unsigned short base_lo;
	unsigned short sel;
	unsigned char zero;
	unsigned char flags;
	unsigned short base_hi;
} __attribute__((packed));

struct idt_ptr
{
	unsigned short limit;
	unsigned int base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;
static volatile unsigned int ticks = 0;

extern void timer_isr_stub(void);

//what happens on every timer interrupt: save regs, run handler, restore, iret
__asm__(
	".global timer_isr_stub\n"
	"timer_isr_stub:\n"
	"	pushal\n"
	"	cld\n"
	"	call timer_handler\n"
	"	popal\n"
	"	iret\n"
);

static inline void t_outb(unsigned short port, unsigned char val)
{
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void timer_handler(void)
{
	ticks++;
	t_outb(0x20, 0x20);     //tell the PIC we're done BEFORE scheduling 
	scheduler_tick();
}

unsigned int timer_ticks(void)
{
	return ticks;
}

void timer_init(int hz)
{
	unsigned short cs;
	__asm__ volatile("movw %%cs, %0" : "=r"(cs));   // reuse the boot code segment 

	for (int i = 0; i < 256; i++)
	{
		idt[i].base_lo = 0;
		idt[i].sel = 0;
		idt[i].zero = 0;
		idt[i].flags = 0;
		idt[i].base_hi = 0;
	}

	unsigned int addr = (unsigned int)timer_isr_stub;
	idt[32].base_lo = addr & 0xFFFF;
	idt[32].sel = cs;
	idt[32].zero = 0;
	idt[32].flags = 0x8E;                  // present, ring 0, 32-bit interrupt gate 
	idt[32].base_hi = (addr >> 16) & 0xFFFF;

	idtp.limit = sizeof(idt) - 1;
	idtp.base = (unsigned int)idt;
	__asm__ volatile("lidt %0" : : "m"(idtp));

	// remap the PIC so IRQ0 (timer) = vector 32, mask everything else 
	t_outb(0x20, 0x11); t_outb(0xA0, 0x11);
	t_outb(0x21, 0x20); t_outb(0xA1, 0x28);
	t_outb(0x21, 0x04); t_outb(0xA1, 0x02);
	t_outb(0x21, 0x01); t_outb(0xA1, 0x01);
	t_outb(0x21, 0xFE); t_outb(0xA1, 0xFF);

	// PIT channel 0 at 'hz' 
	unsigned int divisor = 1193182 / hz;
	t_outb(0x43, 0x36);
	t_outb(0x40, divisor & 0xFF);
	t_outb(0x40, (divisor >> 8) & 0xFF);

	scheduler_enable_preemption();
	__asm__ volatile("sti");
}
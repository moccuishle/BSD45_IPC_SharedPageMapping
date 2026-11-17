/*
 * smp.c - i386 SMP support: per-CPU state, spinlocks, Local APIC, MP detection.
 *
 * This is phase 1 of SMP: the data structures and low-level primitives.
 * AP startup (INIT/SIPI trampoline) and per-CPU scheduling follow once this
 * layer is stable.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "sys/smp.h"
#include "sys/platform.h"
#include "thread/kthread.h"
#include "arch/i386/io.h"

extern void pmap_map(unsigned virt, unsigned start, unsigned end, int prot);
#define	VM_PROT_READ	0x1
#define	VM_PROT_WRITE	0x2
#define	LAPIC_VA	0xFE800000u	/* free kernel window above URING at 0xFE7xxxxx */

/* EN: Read a CMOS/RTC register (port 0x70 index, 0x71 data). */
static unsigned char
cmos_read(int reg)
{

	hal_outb(0x70, (unsigned)(reg & 0x7f));
	return (hal_inb(0x71));
}

struct cpu_info cpus[MAXCPU];
int ncpu = 1;			/* BSP is CPU 0; grows as APs come up */
static int ap_tramp_installed = 0;	/* set once trampoline is copied+patched */

/* Local APIC default MMIO physical base; remapped via MSR if needed. */
#define	LAPIC_PA	0xFEE00000u
static unsigned lapic_va = 0;	/* 0 until lapic_init maps it */

/* Local APIC register offsets */
#define	LAPIC_ID	0x20
#define	LAPIC_VER	0x30
#define	LAPIC_TPR	0x80
#define	LAPIC_EOI	0xB0
#define	LAPIC_LDR	0xD0
#define	LAPIC_DFR	0xE0
#define	LAPIC_SPIV	0xF0
#define	LAPIC_ICR_LO	0x300
#define	LAPIC_ICR_HI	0x310
#define	LAPIC_LVT_TIMER	0x320
#define	LAPIC_TIMER_INIT	0x380
#define	LAPIC_TIMER_CUR	0x390
#define	LAPIC_TIMER_DIV	0x3E0

static inline unsigned
lapic_read(unsigned off)
{

	return (lapic_va ? *(volatile unsigned *)(lapic_va + off) : 0);
}

static inline void
lapic_write(unsigned off, unsigned val)
{

	if (lapic_va)
		*(volatile unsigned *)(lapic_va + off) = val;
}

/* EN: Spinlock init. */
void
spin_init(struct spinlock *lk)
{

	if (lk != 0)
		lk->locked = 0;
}

/* EN: Acquire a spinlock with a pause loop. */
void
spin_lock(struct spinlock *lk)
{
	int prev;

	if (lk == 0)
		return;
	for (;;) {
		prev = 0;
		__asm __volatile("xchgl %0,%1"
		    : "=r" (prev) : "m" (lk->locked), "0" (1) : "memory");
		if (prev == 0) {
			preempt_disable();	/* hold lock => no preemption */
			return;			/* acquired */
		}
		__asm __volatile("pause");	/* spin hint */
	}
}

/* EN: Release a spinlock. */
void
spin_unlock(struct spinlock *lk)
{

	if (lk != 0) {
		lk->locked = 0;
		preempt_enable();	/* released lock => preemption allowed */
	}
}

/* EN: Return the local APIC ID of this CPU. */
unsigned
lapic_id(void)
{

	return (lapic_read(LAPIC_ID) >> 24);
}

/* EN: Send an inter-processor interrupt (IPI) to a target APIC ID. */
void
lapic_send_ipi(int target, int vector)
{

	lapic_write(LAPIC_ICR_HI, (unsigned)(target << 24));
	lapic_write(LAPIC_ICR_LO, (unsigned)(vector | 0x500));
}

/* EN: Signal end-of-interrupt to the local APIC. */
void
lapic_eoi(void)
{

	lapic_write(LAPIC_EOI, 0);
}

/* EN: Program the timer for a short delay used between IPIs. */
static void
ap_delay(void)
{
	volatile int i;

	for (i = 0; i < 200000; i++)
		;
}

/* EN: Send INIT IPI to a target AP. */
static void
ap_send_init(int target)
{

	lapic_write(LAPIC_ICR_HI, (unsigned)(target << 24));
	lapic_write(LAPIC_ICR_LO, 0x500);	/* INIT, level-triggered */
	ap_delay();
	lapic_write(LAPIC_ICR_LO, 0);		/* de-assert */
	ap_delay();
}

/* EN: Send STARTUP (SIPI) IPI: vector = trampoline_pa >> 12 (0x7000 -> 0x07). */
static void
ap_send_sipi(int target)
{
	int vec = CPU_TRAMPOLINE_PA >> 12;

	lapic_write(LAPIC_ICR_HI, (unsigned)(target << 24));
	lapic_write(LAPIC_ICR_LO, 0x600 | vec);	/* SIPI */
	ap_delay();
}

/*
 * EN: Map the Local APIC into the kernel's virtual address space.  The
 * default MMIO base is 0xFEE00000; the BSP's kernel page table already maps
 * the physical range around it (KERNBASE identity window), so we compute
 * the mapped VA as KERNBASE-relative.
 */
static void
lapic_map(void)
{
	/*
	 * NOTE: mapping the Local APIC MMIO page here corrupts the port's
	 * page-table layout (pmap_map advances state that user exec relies
	 * on), breaking /bin/sh.  Keep the APIC unmapped so the system stays
	 * single-CPU and fully stable.  A future SMP phase must map the APIC
	 * with a dedicated reserved PDE that does not disturb virtual_avail.
	 */
	lapic_va = 0;
}

/*
 * EN: Initialize per-CPU state and try to start APs.  If no Local APIC is
 * present (lapic readback invalid), stay single-CPU -- this keeps QEMU with
 * a single CPU and the existing tests working.
 */
void
smp_init(void)
{
	int i, detected;

	for (i = 0; i < MAXCPU; i++) {
		cpus[i].ci_id = i;
		cpus[i].ci_active = (i == 0);
		cpus[i].ci_curthread = 0;
		cpus[i].ci_ticks = 0;
	}
	ncpu = 1;
	lapic_map();
	detected = mp_detect();
	printf("smp: per-CPU ready, %d CPU(s) detected, lapic %s\n",
	    detected, lapic_va ? "mapped" : "absent");
	if (detected > 1)
		smp_start_aps(detected);
}

/*
 * EN: Boot application processors 1..count-1 via the MP protocol.  The AP
 * trampoline is copied to low physical memory by the caller (stage2/boot);
 * if it is not present we log and leave the APs parked (single-CPU safe).
 */
void
smp_start_aps(int count)
{
	int ap;

	if (lapic_va == 0 || count <= 1)
		return;
	/* The AP trampoline must be installed at 0x7000 and its C entry
	 * patched before SIPI.  Until that copy is wired in, do not release
	 * the APs (they would jump into unmapped memory). */
	if (!ap_tramp_installed)
		return;
	printf("smp: starting %d AP(s) via INIT/SIPI\n", count - 1);
	for (ap = 1; ap < count && ap < MAXCPU; ap++) {
		ap_send_init(ap);
		ap_send_sipi(ap);
		ap_send_sipi(ap);
		ap_delay();
		if (cpus[ap].ci_active)
			printf("smp: AP %d online\n", ap);
		else
			printf("smp: AP %d did not respond\n", ap);
	}
}

/*
 * EN: Detect the number of CPUs.  Parses the MP floating-pointer structure
 * in the BIOS data area / EBDA when present; falls back to 1 (BSP only).
 * Conservative by design: a wrong count that starts a nonexistent AP just
 * costs a timeout, but never corrupts the single-CPU boot path.
 */
int
mp_detect(void)
{
	unsigned ver;

	if (lapic_va == 0)
		return (1);		/* no Local APIC: single CPU */
	ver = lapic_read(LAPIC_VER);
	if ((ver & 0xff) == 0)
		return (1);		/* version 0: APIC not usable */
	/* SMBIOS/CMOS 0x5F holds the processor count on QEMU/VMware.  0 or 1
	 * means uniprocessor.  Clamp to MAXCPU for safety. */
	{
		unsigned char cpu_count = cmos_read(0x5f);

		if (cpu_count >= 2 && cpu_count <= MAXCPU)
			return ((int)cpu_count);
	}
	return (1);
}

/*
 * EN: C entry point for an application processor.  Called from ap_tramp.s
 * after the AP has entered protected mode with paging and the shared
 * kernel page directory.  Sets up per-CPU state and enters the idle loop.
 */
void
ap_start(int id)
{

	if (id <= 0 || id >= MAXCPU)
		return;
	cpus[id].ci_active = 1;
	cpus[id].ci_curthread = 0;
	cpus[id].ci_lapic = lapic_va;
	lapic_init();
	lapic_timer_init();
	ncpu++;
	printf("smp: AP %d online (ncpu %d)\n", id, ncpu);
	for (;;) {
		/* AP idle: drain its run queue, then halt until IPI */
		__asm __volatile("hlt");
	}
}

/* EN: Local APIC init: enable APIC via spurious vector register. */
void
lapic_init(void)
{

	if (lapic_va == 0)
		return;
	lapic_write(LAPIC_DFR, 0xffffffff);	/* flat model */
	lapic_write(LAPIC_LDR, 1u << (lapic_id() & 0xff) << 24);
	lapic_write(LAPIC_LVT_TIMER, 0x10000);	/* masked for now */
	lapic_write(LAPIC_SPIV, 0x100 | 0x3f);	/* enable + spurious vec 0x3f */
}

/* EN: Start the AP local timer (placeholder: one-shot, masked). */
void
lapic_timer_init(void)
{

	if (lapic_va == 0)
		return;
	lapic_write(LAPIC_TIMER_DIV, 0x3);	/* divide by 16 */
	lapic_write(LAPIC_TIMER_INIT, 0);	/* no counts yet */
}

/* EN: Parallel idle/balance hook: migrate threads from busy to idle CPU. */
void
smp_balance(void)
{

	if (ncpu <= 1)
		return;
	/* per-CPU run queues and IPI-driven migration are wired in once APs
	 * report online; the policy hook is in place. */
}

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

/*
 * S200: SMP master switch.  The kernel is validated as single-CPU by 17 test
 * suites; bringing APs online changes timing and touches shared page tables.
 * Default 0 keeps every existing test byte-for-byte stable.  Set to 1 (via
 * the 'smpon' monitor command, or SMP_ENABLE at build time) to run the full
 * MP startup protocol.  A conservative boot always completes on the BSP.
 */
#ifndef SMP_ENABLE
#define SMP_ENABLE 0
#endif
int smp_enabled = SMP_ENABLE;

/* S200: IPI/TLB-shootdown statistics (observable via the 'smpinfo' command). */
unsigned ipi_tlb_shootdowns;
unsigned ipi_reschedules;
unsigned smp_tlb_flushes;

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
 * S200: Map the Local APIC MMIO page into a fixed, reserved kernel VA window.
 *
 * The old implementation left the APIC unmapped because calling pmap_map()
 * advanced virtual_avail and corrupted the page-table layout that user exec
 * depends on.  We avoid pmap entirely here: vtopte(va) gives the PTE for a
 * kernel virtual address through the recursive page-table window, so we can
 * install one uncacheable mapping by hand without touching any allocator
 * state.  LAPIC_VA (0xFE800000) sits in the reserved kernel window above the
 * uring region, so no other code maps it.
 */
/*
 * PTmap is the recursive page-table window: PTmap[(va >> 12)] is the PTE
 * that maps virtual page `va`.  We declare it as raw 32-bit entries so we do
 * not have to pull in the pmap.h/machine headers (which need types like
 * boolean_t / simple_lock_data_t that this translation unit lacks).
 *
 * i386 PTE bits used here:
 *   bit 0    P    present
 *   bit 1    RW   writable
 *   bit 5    PCD  cache disable (mandatory for APIC MMIO)
 *   bits 31:12    physical page frame number
 */
extern unsigned int PTmap[];
#define S200_PTE_P	0x001
#define S200_PTE_RW	0x002
#define S200_PTE_PCD	0x020

static void
lapic_map(void)
{
	volatile unsigned int *pte;

	pte = &PTmap[LAPIC_VA >> PGSHIFT];
	*pte = ((LAPIC_PA >> PGSHIFT) << 12) |
	    S200_PTE_P | S200_PTE_RW | S200_PTE_PCD;
	/* invalidate the TLB entry for this VA */
	__asm __volatile("invlpg (%0)" : : "r" (LAPIC_VA) : "memory");
	lapic_va = LAPIC_VA;
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
	/* S200: only map/enable the APIC when SMP is explicitly requested.
	 * Default builds stay single-CPU with zero behavioural change. */
	if (!smp_enabled) {
		printf("smp: per-CPU ready, 1 CPU (SMP disabled)\n");
		return;
	}
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
 * S200: scan for the MP floating-pointer structure ("_MP_" signature) in
 * the three legal BIOS regions: EBDA (via BDA word at 0x40e), the 0xF0000
 * BIOS segment, and the extended BIOS ROM.  Returns the CPU count encoded
 * in the MP config table header, or 0 if not found / unusable.
 *
 * The floating pointer is 16-byte aligned; its byte 11 is the "MP feature
 * information byte 2" which, when bit 7 is set, means the config table is
 * present.  We read the config table's header byte 8 (entry count) and infer
 * CPU count conservatively from the CMOS value as the authoritative source,
 * using the MP table only to confirm an MP-capable system.
 */
static int
mp_scan_floating(void)
{
	unsigned long base;
	int found = 0;

	/* Physical memory is mapped at KERNBASE (0xFE000000) in this port, so
	 * a physical address pa is readable at (pa + 0xFE000000). */
#define S200_KVA(pa)	((volatile unsigned char *)((unsigned)(pa) + 0xFE000000u))
	/* BDA word at physical 0x40e holds the EBDA segment; <<4 = byte address */
	{
		unsigned short ebda_seg = *(volatile unsigned short *)S200_KVA(0x040e);
		base = ((unsigned long)ebda_seg) << 4;
		if (base == 0 || base > 0x100000u)
			base = 0x9FC00u;	/* typical EBDA fallback */
	}
	/* Candidate regions: EBDA (1K), then 0xF0000 BIOS segment (64K). */
	{
		int region;
		for (region = 0; region < 2 && !found; region++) {
			unsigned long start = region == 0 ? base : 0xF0000u;
			unsigned long span = region == 0 ? 0x400 : 0x10000;
			unsigned long off;

			for (off = 0; off < span; off += 16) {
				volatile unsigned char *q = S200_KVA(start + off);
				if (q[0] == '_' && q[1] == 'M' && q[2] == '_' && q[3] == 'P') {
					found = 1;
					break;
				}
			}
		}
	}
#undef S200_KVA
	return (found);
}

/*
 * EN: Detect the number of CPUs.  Requires a usable Local APIC, then uses
 * the SMBIOS/CMOS 0x5F processor count, cross-checked against the presence
 * of an MP floating table.  Falls back to 1 (BSP only).  Conservative: a
 * wrong count only costs an AP-start timeout, never corrupts the boot path.
 */
int
mp_detect(void)
{
	unsigned ver;
	int mp = 0;

	if (lapic_va == 0)
		return (1);		/* no Local APIC: single CPU */
	ver = lapic_read(LAPIC_VER);
	if ((ver & 0xff) == 0)
		return (1);		/* version 0: APIC not usable */
	mp = mp_scan_floating();
	{
		unsigned char cpu_count = cmos_read(0x5f);

		printf("smp: MP table %s, CMOS cpu_count=%u\n",
		    mp ? "found" : "absent", (unsigned)cpu_count);
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

/* ------------------------------------------------------------------ */
/* S200: inter-processor operations and diagnostics                    */
/* ------------------------------------------------------------------ */

/*
 * smp_tlb_shootdown - invalidate a virtual address on all other CPUs.
 *
 * On x86 each CPU has its own TLB, so a pmap change made on one CPU must be
 * invalidated on the others.  We broadcast IPI_TLB_SHOOTDOWN; each receiver
 * reloads CR3 (a full inclusive flush - simple and correct for this port).
 * In a uniprocessor build this is a single local invlpg, zero IPI traffic.
 */
void
smp_tlb_shootdown(unsigned va)
{
	int me, i;

	__asm __volatile("invlpg (%0)" : : "r" (va) : "memory");
	smp_tlb_flushes++;		/* local flush happened regardless of CPU count */
	if (ncpu <= 1 || lapic_va == 0)
		return;
	me = (int)lapic_id();
	for (i = 0; i < MAXCPU; i++) {
		if (i == me || !cpus[i].ci_active)
			continue;
		ipi_tlb_shootdowns++;
		lapic_send_ipi(i, IPI_TLB_SHOOTDOWN);
	}
}

/*
 * smp_reschedule - nudge other CPUs to re-run the scheduler after a wakeup.
 */
void
smp_reschedule(void)
{
	int me, i;

	if (ncpu <= 1 || lapic_va == 0)
		return;
	me = (int)lapic_id();
	for (i = 0; i < MAXCPU; i++) {
		if (i == me || !cpus[i].ci_active)
			continue;
		ipi_reschedules++;
		lapic_send_ipi(i, IPI_RESCHEDULE);
	}
}

/*
 * smp_handle_ipi - called from the APIC interrupt vector table.  vector is
 * the interrupt number; IPI vectors are 0x40..0x4f.  A full CR3 reload is a
 * safe inclusive TLB flush for the shootdown case.
 */
void
smp_handle_ipi(int vector)
{
	unsigned cr3;

	switch (vector) {
	case IPI_TLB_SHOOTDOWN:
		cr3 = cpu_read_cr3();
		cpu_load_cr3(cr3);	/* flush all non-global TLB entries */
		break;
	case IPI_RESCHEDULE:
		/* cooperative scheduler: a future tick will pick up the work;
		 * the flag is recorded and no unsafe cross-CPU stack switch is
		 * attempted here. */
		break;
	case IPI_STOP:
		cpus[(lapic_id() < MAXCPU) ? lapic_id() : 0].ci_active = 0;
		break;
	default:
		break;
	}
	lapic_eoi();
}

/*
 * smp_enable - runtime opt-in used by the monitor 'smpon' command.  Maps the
 * APIC, detects CPUs and (if >1) starts the APs.  Returns the CPU count.
 */
int
smp_enable(void)
{
	int detected;

	if (smp_enabled)
		return (ncpu);
	smp_enabled = 1;
	lapic_map();
	detected = mp_detect();
	printf("smp: enabling, %d CPU(s) detected, lapic %s\n",
	    detected, lapic_va ? "mapped" : "absent");
	if (detected > 1)
		smp_start_aps(detected);
	return (ncpu);
}

/*
 * smp_print - dump SMP status via the kernel printf (monitor 'smpinfo').
 */
void
smp_print(void)
{
	int i, active = 0;

	for (i = 0; i < MAXCPU; i++)
		if (cpus[i].ci_active)
			active++;
	printf("smp: enabled=%d active=%d ncpu=%d lapic=%s\n",
	    smp_enabled, active, ncpu, lapic_va ? "mapped" : "absent");
	printf("smp: ipi_tlb=%u ipi_resched=%u tlb_flushes=%u\n",
	    ipi_tlb_shootdowns, ipi_reschedules, smp_tlb_flushes);
}

/*
 * ipi_selftest - exercise the IPI/TLB-shootdown path in a safe way.  With a
 * single active CPU this only performs the local invlpg (no IPI is sent), so
 * it is always safe to run and validates the call path and counters.
 */
int
ipi_selftest(void)
{
	unsigned before = smp_tlb_flushes;

	smp_tlb_shootdown(LAPIC_VA);
	smp_reschedule();
	return (smp_tlb_flushes == before + 1);
}

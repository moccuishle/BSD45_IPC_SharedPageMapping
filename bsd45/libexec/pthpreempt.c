/*
 * pthpreempt.c - manual demo of kernel-mode thread preemption (KLT).
 *
 * Two threads in the SAME process burn PURE USER CPU (no syscalls) and
 * print their letter.  Because they never enter the kernel voluntarily,
 * the ONLY way to switch is the PIT interrupt landing in ring3 and the
 * scheduler preempting at the irq0 return path.  Interleaved A/B output is
 * therefore direct evidence of true preemptive scheduling.
 *
 * IMPORTANT: do NOT call getticks()/write() inside the burn loop -- a
 * syscall keeps CS in ring0, so the PIT frame is a kernel frame and the
 * scheduler (correctly) defers preemption to the syscall-exit safe point.
 * We calibrate the loop count first, then burn with pure arithmetic only.
 */
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static volatile int run_a, run_b;
static unsigned loops_per_budget;

/* syscall 430 = getticks(): read the 100 Hz PIT tick counter */
static unsigned
getticks(void)
{
	unsigned r;

	__asm__ volatile("int $0x80" : "=a"(r) : "a"(430) : "memory");
	return (r);
}

/* Pure user-mode busy loop -- no syscalls, so IRQs arrive in ring3. */
static void
spin_loop(unsigned n)
{
	volatile unsigned i;

	for (i = 0; i < n; i++)
		;
}

/* Estimate loops that span ~3 timer ticks on THIS machine (QEMU/VMware). */
static void
calibrate(void)
{
	unsigned t0, n = 2000000, dt;

	for (;;) {
		t0 = getticks();
		spin_loop(n);
		dt = getticks() - t0;
		if (dt >= 3)
			break;
		n *= 2;
	}
	loops_per_budget = n;
}

/* Burn ~3 ticks of pure user CPU, guaranteeing PIT scheduling points. */
static void
burn(void)
{

	spin_loop(loops_per_budget);
}

static void *
worker_a(void *arg)
{
	int i, p;

	(void)arg;
	p = sched_getprio();
	printf("A: start prio %d\n", p);
	for (i = 0; i < 10; i++) {
		burn();
		write(1, "A", 1);	/* one syscall per round, outside burn */
	}
	run_a = 1;
	printf("\nA: done\n");
	return (0);
}

static void *
worker_b(void *arg)
{
	int i, p;

	(void)arg;
	p = sched_getprio();
	printf("B: start prio %d\n", p);
	for (i = 0; i < 10; i++) {
		burn();
		write(1, "B", 1);
	}
	run_b = 1;
	printf("\nB: done\n");
	return (0);
}

int
main(void)
{
	pthread_t a, b;
	int ok = 1;

	printf("pthpreempt: two threads, one process\n");
	printf("pthpreempt: main prio %d\n", sched_getprio());
	calibrate();
	printf("pthpreempt: calibrated %u loops\n", loops_per_budget);
	if (pthread_create(&a, 0, worker_a, 0) < 0 ||
	    pthread_create(&b, 0, worker_b, 0) < 0) {
		printf("pthpreempt: create FAIL\n");
		return (1);
	}
	pthread_join(a, 0);
	pthread_join(b, 0);
	printf("pthpreempt: A=%d B=%d\n", run_a, run_b);
	if (!run_a || !run_b)
		ok = 0;
	printf("pthpreempt: %s\n", ok ? "PASS" : "FAIL");
	return (ok ? 0 : 1);
}

/*
 * usermode.c - S3: ramfs + ELF exec with argv/envp + int 0x80 syscalls.
 *
 * A deliberately small VFS: a read-only ramfs table built from the
 * userland images embedded in the kernel.  execve() builds a fresh page
 * directory for the new image, copies argv/envp onto the new user stack
 * and switches CR3 from the syscall path.  This is the bridge to the
 * real 4.4BSD VFS/vm_map work later.
 *
 * Syscall convention (matches 4.4BSD i386 numbers):
 *	%eax	number
 *	%ebx %ecx %edx %esi %edi	arguments
 *	int	$0x80
 * On error %eax holds -errno; the libc stubs translate that to -1/errno.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/termios.h>
#include <sys/ttycom.h>
#include <sys/utsname.h>
#include <sys/time.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>

#include <machine/cpu.h>
#include <machine/pmap.h>

#include "atapi.h"
#include "isofs.h"
#include "vfs.h"
#include "sys/platform.h"
#include "thread/kthread.h"
/* context-switch hooks are declared in sys/platform.h */
#include "sys/platform.h"
#include "sys/platform.h"
#include "sys/platform.h"
#include "vm/zcomp.h"
#include "fs/uring.h"

#define	USER_LOAD	0x08048000
#define	USER_STACK_TOP	0xBFC00000
#define	USER_STACK_SIZE	0x8000
#define	PG_COW		0x00000200	/* software COW marker (PG_FOD bit) */
#define	PG_ZCOMP	0x00000400	/* not-present compressed-page entry */
#define	USER_ARG_PAGE	(USER_STACK_TOP - PAGE_SIZE)
#define	USER_ARGS_MAX	4096

#define	USER_HEAP_START	0x09000000
#define	USER_HEAP_MAX	0x0C000000

#define	SCRATCH_PD	0xFE200000
#define	SCRATCH_PT	0xFE201000
#define	SCRATCH_PAGE	0xFE202000

#define	NFD		16
#define	WNOHANG		1
#define	WUNTRACED	2
#define	MAXARG		16
#define	MAXENV		8

/* 4.4BSD errno values we return */
#define	EPERM		1
#define	ENOENT		2
#define	ESRCH		3
#define	EINTR		4
#define	ENOEXEC		8
#define	EBADF		9
#define	ECHILD		10
#define	ENOMEM		12
#define	ERANGE		34
#define	EACCES		13
#define	EFAULT		14
#define	ENOTDIR		20
#define	EISDIR		21
#define	EINVAL		22
#define	EMFILE		24
#define	ENOTTY		25
#define	EROFS		30
#define	EAGAIN		35
#define	ENOSYS		78

extern const unsigned char hello_elf[], init_elf[];
extern const unsigned int hello_elf_len, init_elf_len;
extern int _ucodesel, _udatasel;
extern void enter_user(unsigned pd, unsigned entry, unsigned esp);
extern void syscall_entry(void);
extern int IdlePTD;		/* physical address of the kernel page dir */
extern int cngetc(void);
extern void cnputc(int c);

#ifndef	O_RDONLY
#define	O_RDONLY	0x0000
#define	O_WRONLY	0x0001
#define	O_RDWR		0x0002
#define	O_ACCMODE	0x0003
#define	O_APPEND	0x0008
#define	O_CREAT		0x0200
#define	O_TRUNC		0x0400
#define	O_EXCL		0x0800
#endif

struct file {
	const char		*f_name;
	const unsigned char	*f_data;
	const unsigned int	*f_len;
};

static struct file ramfs[] = {
	{ "/sbin/init",	init_elf,	&init_elf_len },
	{ "/hello",	hello_elf,	&hello_elf_len },
	{ "/etc/profile", 0, 0 },
	{ 0, 0, 0 }
};

#define	FD_FILE	0
#define	FD_DIR	1
#define	FD_PIPE	2
#define	FD_TMPFILE	3
#define	FD_TMPDIR	4
#define	FD_CONSOLE	5
#define	FD_NULL		6	/* /dev/null */
#define	FD_ZERO		7	/* /dev/zero */
#define	FD_MOUNTS	8	/* /dev/mounts */
#define	FD_PS		9	/* /dev/ps */
#define	FD_VMSTAT	10	/* /dev/vmstat: S17 cache counters */

/*
 * S17: mmap(2).  This port has no demand paging (a page fault stops the
 * machine), so mappings are populated eagerly:
 *	MAP_ANON            - fresh zeroed pages
 *	MAP_PRIVATE + file  - copy of each page cache page (writes are private)
 *	MAP_SHARED + file   - the page cache page itself; msync() writes back
 * The 4.5BSD argument list is (addr,len,prot,flags,fd,pad,pos); this port
 * takes the first five in registers and only supports pos == 0.
 */
#define	MAP_SHARED	0x0001
#define	MAP_PRIVATE	0x0002
#define	MAP_FIXED	0x0010
#define	MAP_ANON	0x1000
#define	PROT_READ	0x01
#define	PROT_WRITE	0x02
#define	PROT_EXEC	0x04
#define	MMAP_BASE	0x10000000
#define	MMAP_LIMIT	0x30000000
#define	MMAP_MAXLEN	0x400000	/* 4 MB per mapping */
#define	NMAP		8

struct vma {
	int		used;
	unsigned	start;
	unsigned	len;
	unsigned	off;
	unsigned	vid;		/* vnode id at map time */
	struct vnode	*vp;
	int		prot;
	int		flags;
};

struct fd {
	const unsigned char	*d;
	unsigned		len;
	unsigned		off;
	int			used;
	int			type;		/* FD_FILE, FD_DIR or FD_PIPE */
	unsigned		lba;		/* directory start lba */
	unsigned		dsize;		/* directory size in bytes */
	unsigned		dpos;		/* next directory record offset */
	int			pipeidx;	/* pipe table slot */
	int			rw;		/* 1 = read end, 2 = write end */
	int			node;		/* tmpfs node index */
	struct vnode		*vp;		/* file/dir through the VFS */
};

/* saved user context (struct sysregs) is declared in sys/platform.h */

#define	NPROC		8
#define	P_FREE		0
#define	P_RUN		1
#define	P_BLOCKED	2
#define	P_ZOMBIE	3
#define	P_STOPPED	4

/*
 * Signal numbers / defaults.  These are the 4.5BSD values; only the ones we
 * actually deliver are listed.
 */
#define	NSIG		32
#define	SIGHUP		1
#define	SIGINT		2
#define	SIGQUIT		3
#define	SIGKILL		9
#define	SIGALRM		14
#define	SIGTERM		15
#define	SIGSTOP		17
#define	SIGTSTP		18
#define	SIGCONT		19
#define	SIGCHLD		20
#define	SIGTTIN		21
#define	SIGTTOU		22
#define	SIGUSR1		30
#define	SIGUSR2		31
/* fault-generated signals (os.md mechanism 1: a bad app must not wedge us) */
#define	SIGILL		4
#define	SIGTRAP		5
#define	SIGFPE		8
#define	SIGBUS		10
#define	SIGSEGV		11

#define	SIG_DFL		0
#define	SIG_IGN		1

#define	SIG_TO_MASK(s)	(1u << (s))

struct uproc {
	int		state;
	int		pipeidx;	/* pipe we are blocked on */
	int		piperw;		/* 1 = read, 2 = write */
	unsigned	pipeat;		/* pending user buffer */
	unsigned	pipelen;	/* pending user length */
	int		pid;		/* 1 = init */
	int		ppid;
	unsigned	pd;		/* page directory */
	unsigned	brk_mapped;	/* next heap page to map */
	struct fd	fds[NFD];
	struct sysregs	regs;		/* context to resume */
	int		exitcode;
	int		wait_pid;	/* pid being waited for (0 = any) */
	unsigned	wait_status;	/* user status pointer for wait4 */
	int		vfork_wait;	/* blocked in vfork() */
	int		vfork_child;	/* shares the parent's address space */
	/* S10.2: signals and process groups */
	unsigned	p_sig;		/* pending signals */
	unsigned	p_sigmask;	/* blocked signals */
	unsigned	p_sigignore;	/* SIG_IGN set */
	unsigned	p_sigcatch;	/* handler installed */
	unsigned	p_sighand[NSIG]; /* user handler addresses */
	int		p_pgrp;		/* process group */
	char		p_cwd[128];	/* current working directory */
	char		p_comm[16];	/* S15: executable basename for /dev/ps */
	int		p_stopsig;	/* signal that stopped us (0 = running) */
	int		p_termsig;	/* signal that killed us (0 = exited) */
	int		p_cont;		/* SIGCONT arrived while stopped */
	unsigned	wait_options;	/* wait4 options while blocked */
	/* S17: mmap bookkeeping */
	unsigned	p_mmap_base;	/* next unused VA for a non-FIXED map */
	struct vma	p_vma[NMAP];
	struct kthread	*p_thread;
	struct kthread	*p_threads;
	int		p_nthreads;
	int		p_next_tid;
	int		aio_wait_token;	/* token this process is blocked on */
	int		uring_wait;	/* blocked in uring_enter */
};

static struct uproc	procs[NPROC];
static struct uproc	*cur;		/* current process */
static struct kthread	*cur_thread;	/* current execution flow */
static int		next_pid = 2;

static unsigned next_pa = 0x200000;
static unsigned page_alloc(void);
static void *map_page(unsigned va, unsigned pa);
static void page_free(unsigned pa);
static int ucopyin(void *udaddr, void *kaddr, unsigned len);
static int ucopyout(void *kaddr, void *udaddr, unsigned len);

/*
 * Physical page allocator.  New pages come from the bump pointer, but a
 * freed page is pushed on a LIFO free list (the page itself stores the next
 * link, reached through the scratch window), so open/close/exec loops reuse
 * memory instead of growing the pool without bound.
 */
static void sig_deliver(struct uproc *, struct sysregs *);
static int console_fg_pgrp;

static unsigned page_free_list;
static unsigned page_nalloc, page_nfree, page_ninuse;

/*
 * COW reference counts (os.md mechanism 2/3).  One byte per physical page.
 * 16384 entries cover the 64 MB guest used by the test harness; pages above
 * that are not COW-shared (page_index()==0 leaves them unaccounted).
 * Keep this table small: the PE/ELF boot loader stages the kernel at
 * 0x10000, and stage 2 lives at 0x90000, so .data must stay below it.
 */
#define	MAX_PHYS_PAGES	(1u << 14)
static unsigned char	page_ref[MAX_PHYS_PAGES];
unsigned	cow_faults;
unsigned	cow_copies;
unsigned	cow_shared;

/* 中文：把物理地址换算为 COW 引用计数表索引，超出表覆盖范围返回 0 */
/* EN: Convert a physical address into an index in the COW reference-count table, returning 0 when out of range */
static unsigned
page_index(unsigned pa)
{
	unsigned i = pa >> 12;

	return (i < MAX_PHYS_PAGES ? i : 0);
}

/* 中文：设置物理页的 COW 引用计数值，超出表范围则忽略 */
/* EN: Set the COW reference count for a physical page, ignoring pages outside the table */
static void
page_ref_set(unsigned pa, unsigned v)
{
	unsigned i = page_index(pa);

	if (i != 0)
		page_ref[i] = (unsigned char)v;
}

/* 中文：读取物理页的 COW 引用计数，超出表范围的页视作独占返回 1 */
/* EN: Get the COW reference count of a physical page, returning 1 (treated as unshared) for pages outside the table */
static unsigned
page_ref_get(unsigned pa)
{
	unsigned i = page_index(pa);

	return (i != 0 ? page_ref[i] : 1);
}

/* 中文：递增物理页的 COW 引用计数，上限 255，超出表范围则忽略 */
/* EN: Increment a physical page's COW reference count up to 255, ignoring pages outside the table */
static void
page_ref_inc(unsigned pa)
{
	unsigned i = page_index(pa);

	if (i != 0 && page_ref[i] < 255)
		page_ref[i]++;
}

/* 中文：释放对物理页的一次引用：引用计数大于 1 则递减，否则把页放回空闲链表 */
/* EN: Release one reference to a physical page: decrement the count if greater than 1, otherwise return the page to the free list */
static void
page_put(unsigned pa)
{
	unsigned i = page_index(pa);

	if (i != 0 && page_ref[i] > 1) {
		page_ref[i]--;
		return;
	}
	if (i != 0)
		page_ref[i] = 0;
	page_free(pa);
}

#define	CD_IMAGE_VA	0xFE400000	/* kernel window for a file read from CD */
static unsigned cd_mapped;		/* bytes mapped at CD_IMAGE_VA */

/*
 * Map (lazily) a kernel window big enough for one image read from the
 * ISO9660 root.  Physical pages come from the bump allocator, so they are
 * contiguous, but we map them one page at a time.
 */
/* 中文：惰性映射一块内核窗口，用于容纳从 ISO9660 根读取的单个映像；超限返回 0，否则返回窗口虚拟地址 */
/* EN: Lazily map a kernel window large enough for one image read from the ISO9660 root; returns 0 if too large, else the window VA */
static unsigned char *
cd_map(unsigned size)
{
	unsigned need = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	unsigned i;

	if (need == 0 || need > 4 * 1024 * 1024)
		return (0);
	for (i = cd_mapped; i < need; i += PAGE_SIZE) {
		unsigned pa = page_alloc();

		pmap_map((vm_offset_t)(CD_IMAGE_VA + i), (vm_offset_t)pa,
		    (vm_offset_t)pa + PAGE_SIZE,
		    VM_PROT_READ | VM_PROT_WRITE);
	}
	cd_mapped = need;
	return ((unsigned char *)CD_IMAGE_VA);
}
static unsigned umask_val = 022;

/* 中文：VFS 物理页分配包装：分配一页并返回其物理地址 */
/* EN: VFS physical page allocation wrapper: allocate one page and return its physical address */
unsigned
vfs_page_alloc(void)
{

	return (page_alloc());
}

/* 中文：VFS 物理页释放包装：归还一页物理内存 */
/* EN: VFS physical page free wrapper: return one physical page to the allocator */
void
vfs_page_free(unsigned pa)
{

	page_free(pa);
}

/*
 * Drop a kernel-window mapping (TMPFS_VA / scratch style).  The PTE lives in
 * kernel_pmap, so clearing it and flushing the TLB is enough.
 */
/* 中文：解除一个内核虚拟地址处的页映射：清 PTE 并刷新 TLB */
/* EN: Unmap the page at a kernel virtual address: clear its PTE and flush the TLB */
void
vfs_page_unmap(unsigned va)
{
	struct pte *pte;

	pte = pmap_pte(kernel_pmap, (vm_offset_t)va);
	if (pte != 0)
		*(int *)pte = 0;
	tlbflush();
}

/* 中文：返回页分配器的统计：已分配、已释放与当前在用页数 */
/* EN: Return page allocator statistics: pages allocated, freed and currently in use */
void
vfs_page_stats(unsigned *nalloc, unsigned *nfreed, unsigned *ninuse)
{

	*nalloc = page_nalloc;
	*nfreed = page_nfree;
	*ninuse = page_ninuse;
}

static int zcomp_reclaim(int n);
static int in_zcomp;
static int user_ok(unsigned p, unsigned len);
static int user_ok_w(unsigned p, unsigned len);
static void proc_sched(void);

/* 中文：分配一页物理内存：内存紧张时先尝试压缩回收私有匿名页，否则从空闲链表取或推进 bump 指针 */
/* EN: Allocate a physical page: under memory pressure first try to reclaim private anonymous pages via compression, then pop the free list or bump the pointer */
static unsigned
page_alloc(void)
{
	unsigned pa;

	/*
	 * Memory-pressure hook: before bumping into more RAM, try to compress
	 * a few private anonymous pages.  The compressed pages stay in RAM,
	 * so this trades CPU for headroom instead of going to swap (there is
	 * no swap device in this port).
	 * 内存压力钩子：在继续向物理内存推进之前，先压缩少量进程私有匿名页。
	 * 压缩后的页仍在 RAM 中，用 CPU 换空间，而不是写 swap（本端口没有
	 * swap 设备）。
	 */
	if (page_free_list == 0 && page_ninuse > 4096 && !in_zcomp &&
	    cur != 0)
		(void)zcomp_reclaim(8);
	if (page_free_list != 0) {
		unsigned *p = map_page(SCRATCH_PAGE, page_free_list);

		pa = page_free_list;
		page_free_list = *p;
		page_nalloc++;
		page_ninuse++;
		page_ref_set(pa, 1);
		return (pa);
	}
	pa = next_pa;
	next_pa += PAGE_SIZE;
	if (next_pa >= 0x3f000000)
		panic("usermode: out of physical memory");
	page_nalloc++;
	page_ninuse++;
	page_ref_set(pa, 1);
	return (pa);
}

/* 中文：释放一页物理内存：忽略非法地址，把页压入 LIFO 空闲链表并更新引用计数与统计 */
/* EN: Free a physical page: ignore invalid addresses, push it on the LIFO free list and update refcount and statistics */
static void
page_free(unsigned pa)
{
	unsigned *p;

	if (pa == 0 || (pa & (PAGE_SIZE - 1)) != 0)
		return;
	p = map_page(SCRATCH_PAGE, pa);
	*p = page_free_list;
	page_free_list = pa;
	page_ref_set(pa, 0);
	page_nfree++;
	if (page_ninuse > 0)
		page_ninuse--;
}

/*
 * Release a whole user address space: every user page, every page table and
 * the page directory itself.  The caller must have switched CR3 away from
 * this directory first.  Entries at/above PPDROFF are kernel mappings and
 * are never touched.
 */
/* 中文：释放整个用户地址空间：逐个释放用户页、压缩页槽、页表与页目录，内核映射区不碰；调用前须已切换 CR3 */
/* EN: Release a whole user address space: free every user page, compressed-page slot, page table and the directory; kernel mappings untouched; caller must have switched CR3 away first */
static void
free_address_space(unsigned pdpa)
{
	unsigned *pd = map_page(SCRATCH_PD, pdpa);
	int i, j;

	for (i = 0; i < 0x3f6; i++) {
		unsigned *pt;

		if ((pd[i] & PG_V) == 0)
			continue;
		pt = map_page(SCRATCH_PT, pd[i] & PG_FRAME);
		for (j = 0; j < PAGE_SIZE / 4; j++) {
			if (pt[j] & PG_ZCOMP) {
				/* compressed page: release its pool slot */
				zcomp_free_slot((pt[j] & PG_FRAME) >> 12);
				pt[j] = 0;
			} else if (pt[j] & PG_V)
				page_put(pt[j] & PG_FRAME);
		}
		page_free(pd[i] & PG_FRAME);
		pd[i] = 0;
	}
	page_free(pdpa);
}

/* 中文：把物理页 pa 映射可读写到内核临时虚拟地址 va，并返回该虚拟地址 */
/* EN: Map physical page pa read-write at the kernel scratch virtual address va and return that address */
static void *
map_page(unsigned va, unsigned pa)
{

	pmap_map((vm_offset_t)va, (vm_offset_t)pa,
	    (vm_offset_t)pa + PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE);
	return ((void *)va);
}

/* 中文：把物理页 pa 映射到临时窗口并整页清零 */
/* EN: Map physical page pa into the scratch window and zero the entire page */
static void
zero_page(unsigned pa)
{
	char *p = map_page(SCRATCH_PAGE, pa);
	int i;

	for (i = 0; i < PAGE_SIZE; i++)
		p[i] = 0;
}

/* 中文：创建用户页目录：分配一页并清零，仅共享内核高端与 proc0 内核栈映射，返回其物理地址 */
/* EN: Create a user page directory: allocate and zero a page, share only the kernel high-half and proc0 stack mappings, return its physical address */
static unsigned
user_pd_create(void)
{
	unsigned pdpa = page_alloc();
	unsigned *pd = map_page(SCRATCH_PD, pdpa);
	int i;

	for (i = 0; i < PAGE_SIZE / 4; i++)
		pd[i] = 0;
	/* share kernel high-half + proc0 kernel stack mappings */
	for (i = 0x3f6; i <= 0x3fa; i++)
		pd[i] = ((unsigned *)kernel_pmap->pm_pdir)[i];
	return (pdpa);
}

/*
 * Map one user page in a *specific* page directory.  The caller passes the
 * directory because after a fork/exec SCRATCH_PD no longer points at the
 * process we are editing (that bug silently unmapped the parent's heap).
 */
/* 中文：在指定页目录中把用户虚拟地址 va 映射到物理页 pa（始终可写、用户态可访问），必要时分配页表 */
/* EN: Map user virtual address va to physical page pa in a specific page directory (always writable, user accessible), allocating a page table if needed */
static void
user_map(unsigned pdpa, unsigned va, unsigned pa)
{
	unsigned pdi = va >> 22, pti = (va >> 12) & 0x3ff;
	unsigned *pd = map_page(SCRATCH_PD, pdpa);
	unsigned *pt;
	unsigned ptpa;
	int i;

	if ((pd[pdi] & PG_V) == 0) {
		ptpa = page_alloc();
		pt = map_page(SCRATCH_PT, ptpa);
		for (i = 0; i < PAGE_SIZE / 4; i++)
			pt[i] = 0;
		pd[pdi] = ptpa | PG_V | PG_RW | PG_u;
	} else {
		ptpa = pd[pdi] & PG_FRAME;
		pt = map_page(SCRATCH_PT, ptpa);
	}
	pt[pti] = pa | PG_V | PG_RW | PG_u;
}

/*
 * S17: map/unmap a single user page with explicit write permission and
 * inspect an existing PTE.  These are used by mmap()/munmap(); user_map()
 * above stays the "always writable" helper used by exec/brk/stack.
 */
/* 中文：查出用户地址 va 对应页表项指针，通过 ptp 回传；无有效页表返回 0 并把 *ptp 置空 */
/* EN: Look up the page-table entry pointer for user address va via ptp; returns 0 and sets *ptp to NULL when no page table exists */
static unsigned
user_pte_ptr(unsigned pdpa, unsigned va, unsigned **ptp)
{
	unsigned pdi = va >> 22, pti = (va >> 12) & 0x3ff;
	unsigned *pd = map_page(SCRATCH_PD, pdpa);
	unsigned *pt;

	if ((pd[pdi] & PG_V) == 0) {
		*ptp = 0;
		return (0);
	}
	pt = map_page(SCRATCH_PT, pd[pdi] & PG_FRAME);
	*ptp = pt;
	return (pti);
}

/* 中文：在指定页目录中把 va 映射到 pa 并按 prot 设置写权限，供 mmap 使用（区别于始终可写的 user_map） */
/* EN: Map va to pa in a specific page directory with write permission per prot, used by mmap (unlike the always-writable user_map) */
static void
user_map_prot(unsigned pdpa, unsigned va, unsigned pa, int prot)
{
	unsigned pdi = va >> 22, pti = (va >> 12) & 0x3ff;
	unsigned *pd = map_page(SCRATCH_PD, pdpa);
	unsigned *pt;
	unsigned ptpa;
	int i;

	if ((pd[pdi] & PG_V) == 0) {
		ptpa = page_alloc();
		pt = map_page(SCRATCH_PT, ptpa);
		for (i = 0; i < PAGE_SIZE / 4; i++)
			pt[i] = 0;
		pd[pdi] = ptpa | PG_V | PG_RW | PG_u;
	} else {
		ptpa = pd[pdi] & PG_FRAME;
		pt = map_page(SCRATCH_PT, ptpa);
	}
	pt[pti] = pa | PG_V | PG_u |
	    ((prot & PROT_WRITE) ? PG_RW : 0);
}

/* 中文：解除用户地址 va 的映射：清对应页表项并刷新 TLB，供 munmap 使用 */
/* EN: Unmap user address va: clear its page-table entry and flush the TLB, used by munmap */
static void
user_unmap(unsigned pdpa, unsigned va)
{
	unsigned *pt;
	unsigned pti = user_pte_ptr(pdpa, va, &pt);

	if (pt != 0) {
		pt[pti] = 0;
		tlbflush();
	}
}

/* 中文：查出用户地址 va 当前映射的物理页，未映射或不存在返回 0 */
/* EN: Return the physical page currently mapped at user address va, or 0 if unmapped/absent */
static unsigned
user_getpa(unsigned pdpa, unsigned va)
{
	unsigned *pt;
	unsigned pti = user_pte_ptr(pdpa, va, &pt);

	if (pt == 0 || (pt[pti] & PG_V) == 0)
		return (0);
	return (pt[pti] & PG_FRAME);
}

/* ---------------------------------------------------------------- mmap */

/* 中文：为进程 p 分配一个空闲的 VMA 槽位，用完返回 0 */
/* EN: Allocate a free VMA slot for process p, returning 0 when none is available */
static struct vma *
vma_alloc(struct uproc *p)
{
	int i;

	for (i = 0; i < NMAP; i++)
		if (!p->p_vma[i].used)
			return (&p->p_vma[i]);
	return (0);
}

/* 中文：查找包含地址 addr 的 VMA 槽位，找到返回指针，否则返回 0 */
/* EN: Find the VMA slot containing address addr, returning its pointer or 0 if none */
static struct vma *
vma_find(struct uproc *p, unsigned addr)
{
	int i;

	for (i = 0; i < NMAP; i++)
		if (p->p_vma[i].used && addr >= p->p_vma[i].start &&
		    addr < p->p_vma[i].start + p->p_vma[i].len)
			return (&p->p_vma[i]);
	return (0);
}

/* 中文：检查 [start, start+len) 是否与进程 p 的任何已有 VMA 区间重叠 */
/* EN: Check whether [start, start+len) overlaps any existing VMA of process p */
static int
vma_overlaps(struct uproc *p, unsigned start, unsigned len)
{
	int i;

	for (i = 0; i < NMAP; i++) {
		struct vma *v = &p->p_vma[i];

		if (!v->used)
			continue;
		if (start < v->start + v->len && v->start < start + len)
			return (1);
	}
	return (0);
}

/*
 * mmap(addr, len, prot, flags, fd): returns the user address or -errno.
 */
/* 中文：mmap 核心实现：校验参数与文件描述符，确定映射起始地址并登记一个 VMA；页面在首次访问时才分配 */
/* EN: mmap core: validate arguments and fd, choose the mapping start address and register a VMA; pages are allocated lazily on first touch */
static int
do_mmap(unsigned addr, unsigned len, int prot, int flags, int fd)
{
	struct vma *v;
	struct vnode *vp = 0;
	unsigned vid = 0, start;
	int share = flags & (MAP_SHARED | MAP_PRIVATE);
	int file = (flags & MAP_ANON) == 0;

	if (len == 0 || len > MMAP_MAXLEN)
		return (-EINVAL);
	len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	if (share == 0 || share == (MAP_SHARED | MAP_PRIVATE))
		return (-EINVAL);
	if ((flags & MAP_FIXED) && (addr & (PAGE_SIZE - 1)))
		return (-EINVAL);
	if (file) {
		if (fd < 0 || fd >= NFD || !cur->fds[fd].used ||
		    cur->fds[fd].vp == 0 ||
		    cur->fds[fd].vp->type != VREG)
			return (-EBADF);
		vp = cur->fds[fd].vp;
		if (share == MAP_SHARED && vp->ops->write == 0 && prot &
		    PROT_WRITE)
			return (-EROFS);
		vp->ref++;
		vid = pagecache_key(vp);
	}
	if (flags & MAP_FIXED) {
		start = addr;
		if (start < MMAP_BASE || start + len > MMAP_LIMIT ||
		    vma_overlaps(cur, start, len)) {
			if (vp)
				vn_put(vp);
			return (-EINVAL);
		}
	} else {
		start = (cur->p_mmap_base + PAGE_SIZE - 1) &
		    ~(PAGE_SIZE - 1);
		if (start + len > MMAP_LIMIT) {
			if (vp)
				vn_put(vp);
			return (-ENOMEM);
		}
	}
	v = vma_alloc(cur);
	if (v == 0) {
		if (vp)
			vn_put(vp);
		return (-ENOMEM);
	}
	/*
	 * Pages are populated on the first fault by mmap_fault(), not here:
	 * mmap only records the VMA so a large mapping costs no memory until
	 * it is touched (os.md mechanism 3, demand paging).
	 * 页面在首次访问时由 mmap_fault() 填充；mmap 只记录 VMA，大映射在
	 * 未触碰前不占物理页（os.md 机制 3，按需分页）。
	 */
	bzero(v, sizeof(*v));
	v->used = 1;
	v->start = start;
	v->len = len;
	v->off = 0;
	v->vid = vid;
	v->vp = vp;
	v->prot = prot;
	v->flags = flags;
	if (!(flags & MAP_FIXED))
		cur->p_mmap_base = start + len;
	return ((int)start);
}

/* 中文：按需填充 mmap 映射页：匿名映射分配零页，共享文件映射页缓存页本身，私有映射拷贝一份并解除页缓存引用 */
/* EN: Demand-fill an mmap page: anonymous mappings get a zeroed page, shared file mappings map the cache page itself, private mappings copy it and drop the cache reference */
static int
mmap_fault(unsigned va)
{
	struct vma *v = vma_find(cur, va);
	unsigned idx, pa;
	unsigned char *src;

	if (v == 0)
		return (0);
	idx = (v->off + (va - v->start)) / PAGE_SIZE;
	if (v->vp == 0) {
		/* anonymous mapping: zero page on first touch */
		pa = page_alloc();
		zero_page(pa);
		user_map_prot(cur->pd, va, pa, v->prot);
		return (1);
	}
	pa = pagecache_get(v->vp, idx, &src);
	if (pa == 0)
		return (0);
	if (v->flags & MAP_SHARED) {
		/* shared file page: map the page-cache page itself */
		user_map_prot(cur->pd, va, pa, v->prot);
		return (1);
	}
	/* private file page: copy the cache page into a process-owned page */
	{
		unsigned pa2 = page_alloc();
		char *dst = map_page(SCRATCH_PAGE, pa2);

		zero_page(pa2);
		bcopy(src, dst, PAGE_SIZE);
		user_map_prot(cur->pd, va, pa2, v->prot);
		pagecache_unref(v->vp, idx);
	}
	return (1);
}

/* 中文：压缩页缺页重填：把 PG_ZCOMP 压缩槽的页解压回物理页，重装 PTE 并激活地址空间；失败返回 0 */
/* EN: Refill a compressed-page fault: decompress a PG_ZCOMP slot back into a physical page, reinstall the PTE and activate the address space; returns 0 on failure */
static int
zcomp_refault(unsigned va)
{
	unsigned *pt;
	unsigned pti, slot, pa;
	char *dst;

	pti = user_pte_ptr(cur->pd, va & ~(PAGE_SIZE - 1), &pt);
	if (pt == 0 || (pt[pti] & PG_V) != 0 ||
	    (pt[pti] & PG_ZCOMP) == 0)
		return (0);
	slot = (pt[pti] & PG_FRAME) >> 12;
	pa = page_alloc();
	dst = map_page(SCRATCH_PAGE, pa);
	if (zcomp_load(slot, dst) < 0) {
		page_put(pa);
		pt[pti] = 0;
		return (0);
	}
	pt[pti] = pa | PG_V | PG_RW | PG_u;
	hal_address_space_activate(cur->pd);
	return (1);
}

/*
 * Compress up to n private anonymous pages of the current process.  Only
 * present, writable, user, non-COW pages outside a VMA are candidates;
 * file-backed and shared pages are left alone (they can be re-read).
 * 压缩当前进程最多 n 个私有匿名页。候选必须 present、可写、用户态、
 * 非 COW，且不在 VMA 内；文件页与共享页不压缩（可以重新读取）。
 */
/*
 * User-space asynchronous I/O ABI (os.md mechanism 5).
 *
 * submit returns a token immediately; the VFS aio worker runs the shared
 * read_iter/write_iter backend and completes through a deferred softirq callback.
 * A read completes into the per-slot kernel page and is copied out before
 * the waiting process is woken; a write copies the user buffer in at submit.
 * 用户态异步 I/O ABI：submit 立即返回 token；VFS aio worker 执行共享的
 * read_iter/write_iter 后端，并通过延迟软中断回调完成。读先落到每槽内核
 * 页，在唤醒等待进程前拷回用户；写在提交时先把用户缓冲拷入。
 */
#define	NAIO		8
#define	AIO_VA		0xFE500000

struct aio_slot {
	int		 a_used;
	int		 a_token;
	int		 a_op;
	int		 a_pid;
	int		 a_done;
	int		 a_woken;
	int		 a_result;
	unsigned	 a_ubuf;
	unsigned	 a_len;
	unsigned	 a_off;
	unsigned a_user_data;	/* ring CQE payload */
	void		*a_ring;	/* struct uring_ring * when from a ring */
	unsigned char	*a_kbuf;
	struct uproc	*a_proc;
	struct kiocb	 a_ki;
};

#define	NURING		2
#define	URING_KVA	0xFE700000
#define	URING_UVA	0x31000000

struct uring_ring {
	int		 r_used;
	struct uproc	*r_proc;
	unsigned	 r_pd;
	unsigned	 r_uva;
	unsigned	 r_pa;
	unsigned	 r_entries;
	int		 r_wait_pid;
	int		 r_wait_result;
	int		 r_expected;	/* completions this batch must produce */
	int		 r_done;	/* completions seen for this batch */
	struct uring_header *r_hdr;
	struct uring_sqe *r_sq;
	struct uring_cqe *r_cq;
};

static struct uring_ring rings[NURING];
static int		uring_inited;

static void uring_wake(struct uring_ring *r);
static int uring_cqe_push(struct uring_ring *r, unsigned user_data, int res);

static struct aio_slot	aios[NAIO];
static int		aio_next_token;

static void aio_soft(void *arg);

/* 中文：初始化用户态异步 I/O：清零 aio 槽、为每个槽分配并映射内核缓冲页，注册延迟软中断处理 */
/* EN: Initialize userland async I/O: clear aio slots, allocate and map a kernel buffer page per slot, and register the deferred softirq handler */
void
aio_user_init(void)
{
	int i;
	unsigned pa;

	bzero(aios, sizeof(aios));
	aio_next_token = 1;
	for (i = 0; i < NAIO; i++) {
		pa = vfs_page_alloc();
		pmap_map((vm_offset_t)(AIO_VA + i * PAGE_SIZE),
		    (vm_offset_t)pa, (vm_offset_t)pa + PAGE_SIZE,
		    VM_PROT_READ | VM_PROT_WRITE);
		aios[i].a_kbuf = (unsigned char *)(AIO_VA + i * PAGE_SIZE);
	}
	printf("aio: %d submission slots at %x\n", NAIO, AIO_VA);
}

/* 中文：异步 I/O 完成回调：记录结果，读操作把内核缓冲拷回用户，若来自 ring 则压 CQE 并唤醒，否则触发 aio_soft */
/* EN: Async I/O completion callback: record the result, copy kernel buffer back to user for reads, push a CQE and wake if from a ring, else raise aio_soft */
static void
aio_complete_kiocb(struct kiocb *ki, int r)
{
	struct aio_slot *s = (struct aio_slot *)ki->ki_private;
	struct uproc *save = cur;
	unsigned savepd = hal_address_space_current();

	if (s == 0)
		return;
	s->a_result = r;
	s->a_done = 1;
	if (s->a_op == KI_READ && r > 0) {
		cur = s->a_proc;
		if (cur != 0) {
			hal_address_space_activate(cur->pd);
			if (user_ok_w(s->a_ubuf, (unsigned)r))
				bcopy(s->a_kbuf, (void *)s->a_ubuf,
				    (unsigned)r);
		}
		cur = save;
		hal_address_space_activate(savepd);
	}
	if (s->a_ring != 0) {
		struct uring_ring *rg = (struct uring_ring *)s->a_ring;
		(void)uring_cqe_push(rg, s->a_user_data, r);
		rg->r_done++;
		uring_wake(rg);
		return;
	}
	softirq_defer(aio_soft, s);
}

/* 中文：aio 完成软中断：找到等待该 token 的进程，把结果写入其 regs.eax、置为可运行并释放槽位；ring 完成不走此路 */
/* EN: aio completion softirq: find the process waiting on the token, write the result into regs.eax, make it runnable and free the slot; ring completions skip this */
static void
aio_soft(void *arg)
{
	struct aio_slot *s = (struct aio_slot *)arg;
	struct uproc *p = 0;
	int i;

	if (s == 0 || s->a_woken)
		return;
	if (s->a_ring != 0)
		return;			/* ring completion woke the caller */
	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_FREE && procs[i].pid == s->a_pid) {
			p = &procs[i];
			break;
		}
	if (p != 0 && p->state == P_BLOCKED &&
	    p->aio_wait_token == s->a_token) {
		p->aio_wait_token = 0;
		p->regs.eax = (unsigned)s->a_result;
		p->state = P_RUN;
		vn_put(s->a_ki.ki_vp);
		s->a_used = 0;
		s->a_woken = 1;
	}
}

/* 中文：提交一次异步读写：校验 fd、长度与用户缓冲，占用一个 aio 槽，写时拷入用户数据并发起异步 VFS 读写，成功返回 token */
/* EN: Submit one async read/write: validate fd, length and user buffer, reserve an aio slot, copy in user data for writes and start async VFS I/O, returning a token on success */
static int
aio_submit_sys(int fd, int op, unsigned ubuf, unsigned len, unsigned off)
{
	struct aio_slot *s = 0;
	struct vnode *vp;
	int i, token, r;

	if (fd < 0 || fd >= NFD || !cur->fds[fd].used ||
	    cur->fds[fd].vp == 0)
		return (-EBADF);
	vp = cur->fds[fd].vp;
	if (len == 0 || len > PAGE_SIZE)
		return (-EINVAL);
	if (op == KI_READ ? !user_ok_w(ubuf, len) : !user_ok(ubuf, len))
		return (-EFAULT);
	for (i = 0; i < NAIO; i++)
		if (!aios[i].a_used) {
			s = &aios[i];
			break;
		}
	if (s == 0)
		return (-EAGAIN);
	{
		unsigned char *kbuf = s->a_kbuf;

		bzero(s, sizeof(*s));
		s->a_kbuf = kbuf;
	}
	s->a_used = 1;
	token = aio_next_token++;
	if (aio_next_token <= 0)
		aio_next_token = 1;
	s->a_token = token;
	s->a_op = op;
	s->a_pid = cur->pid;
	s->a_proc = cur;
	s->a_ubuf = ubuf;
	s->a_len = len;
	s->a_off = off;
	if (op == KI_WRITE) {
		if (ucopyin((void *)ubuf, s->a_kbuf, len) < 0) {
			s->a_used = 0;
			return (-EFAULT);
		}
	} else
		bzero(s->a_kbuf, len);

	s->a_ki.ki_vp = vp;
	s->a_ki.ki_op = op;
	s->a_ki.ki_flags = KI_ASYNC;
	s->a_ki.ki_off = off;
	s->a_ki.ki_buf = s->a_kbuf;
	s->a_ki.ki_len = len;
	s->a_ki.ki_complete = aio_complete_kiocb;
	s->a_ki.ki_private = s;
	vp->ref++;
	r = (op == KI_READ) ? vfs_read_async(&s->a_ki) :
	    vfs_write_async(&s->a_ki);
	if (r != EIOCBQUEUED) {
		vn_put(vp);
		s->a_used = 0;
		return (r < 0 ? r : -EIO);
	}
	return (token);
}

/* 中文：等待异步 I/O token：已完成则立即返回结果并释放槽，否则保存上下文、阻塞进程并调度，由 aio_soft 唤醒 */
/* EN: Wait for an async I/O token: return the result and free the slot if done, else save context, block the process and schedule, to be woken by aio_soft */
static int
aio_wait_sys(int token, struct sysregs *r)
{
	struct aio_slot *s;
	int i, res;

	for (i = 0; i < NAIO; i++)
		if (aios[i].a_used && aios[i].a_token == token) {
			s = &aios[i];
			goto found;
		}
	return (-EINVAL);
found:
	if (s->a_done) {
		res = s->a_result;
		vn_put(s->a_ki.ki_vp);
		s->a_used = 0;
		return (res);
	}
	/*
	 * Block the calling process; aio_soft() writes the result into
	 * p->regs.eax and makes it runnable again.
	 * 阻塞调用进程；aio_soft() 把结果写进 p->regs.eax 并让它重新可运行。
	 */
	cur->aio_wait_token = token;
	cur->regs = *r;
	cur->state = P_BLOCKED;
	proc_sched();
	return (0);
}

/* 中文：轮询异步 I/O token：完成则返回结果并释放槽，未完成返回 -EAGAIN，token 无效返回 -EINVAL */
/* EN: Poll an async I/O token: return result and free the slot if done, -EAGAIN if still pending, -EINVAL for an invalid token */
static int
aio_poll_sys(int token)
{
	struct aio_slot *s;
	int i, res;

	for (i = 0; i < NAIO; i++)
		if (aios[i].a_used && aios[i].a_token == token) {
			s = &aios[i];
			if (!s->a_done)
				return (-EAGAIN);
			res = s->a_result;
			vn_put(s->a_ki.ki_vp);
			s->a_used = 0;
			return (res);
		}
	return (-EINVAL);
}

/* 中文：向 io_uring 环的完成队列压入一个 CQE（user_data/result），队列满则增加 overflow 并返回 0 */
/* EN: Push a CQE (user_data/result) onto an io_uring ring's completion queue; on overflow increment the counter and return 0 */
static int
uring_cqe_push(struct uring_ring *rg, unsigned user_data, int result)
{
	struct uring_cqe *cq;

	if ((rg->r_hdr->cq_tail - rg->r_hdr->cq_head) >= rg->r_entries) {
		rg->r_hdr->cq_overflow++;
		return (0);
	}
	cq = &rg->r_cq[rg->r_hdr->cq_tail % rg->r_entries];
	cq->cq_user_data = user_data;
	cq->cq_result = result;
	cq->cq_flags = 0;
	rg->r_hdr->cq_tail++;
	return (1);
}

/* 中文：唤醒等待 io_uring 的进程：当本批完成数达到期望值时把结果写入其 regs.eax 并置为可运行 */
/* EN: Wake the process waiting on an io_uring ring: when this batch's completions reach the expected count, write the result into regs.eax and make it runnable */
static void
uring_wake(struct uring_ring *rg)
{
	struct uproc *p = 0;
	int i;

	if (rg->r_wait_pid == 0)
		return;
	if (rg->r_done < rg->r_expected)
		return;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_FREE && procs[i].pid == rg->r_wait_pid) {
			p = &procs[i];
			break;
		}
	if (p != 0 && p->state == P_BLOCKED) {
		p->uring_wait = 0;
		p->regs.eax = (unsigned)rg->r_wait_result;
		p->state = P_RUN;
	}
	rg->r_wait_pid = 0;
}

/* 中文：创建 io_uring 环：分配空闲环与一页共享内存，映射内核/用户视图，初始化 SQE/CQ 头并返回用户虚拟地址 */
/* EN: Set up an io_uring ring: allocate a free ring and a shared page, map kernel/user views, initialize SQE/CQ headers and return the user VA */
static int
uring_setup_sys(unsigned entries)
{
	struct uring_ring *rg = 0;
	struct uring_header *h;
	unsigned pa, kva, uva;
	int i;

	if (!uring_inited) {
		bzero(rings, sizeof(rings));
		uring_inited = 1;
	}
	if (entries == 0 || entries > URING_ENTRIES)
		entries = URING_ENTRIES;
	for (i = 0; i < NURING; i++)
		if (!rings[i].r_used) {
			rg = &rings[i];
			break;
		}
	if (rg == 0)
		return (-ENOMEM);
	pa = vfs_page_alloc();
	kva = URING_KVA + (unsigned)i * PAGE_SIZE;
	pmap_map((vm_offset_t)kva, (vm_offset_t)pa,
	    (vm_offset_t)pa + PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE);
	bzero((void *)kva, PAGE_SIZE);
	uva = URING_UVA + (unsigned)i * 0x10000;
	user_map_prot(cur->pd, uva, pa, PROT_READ | PROT_WRITE);
	hal_address_space_activate(cur->pd);

	rg->r_used = 1;
	rg->r_proc = cur;
	rg->r_pd = cur->pd;
	rg->r_uva = uva;
	rg->r_pa = pa;
	rg->r_entries = entries;
	rg->r_wait_pid = 0;
	rg->r_hdr = (struct uring_header *)kva;
	rg->r_sq = (struct uring_sqe *)(kva + URING_OFF_SQ);
	rg->r_cq = (struct uring_cqe *)(kva + URING_OFF_CQ);
	h = rg->r_hdr;
	h->magic = URING_MAGIC;
	h->entries = entries;
	h->sq_head = h->sq_tail = 0;
	h->cq_head = h->cq_tail = 0;
	printf("uring: ring %d entries %d uva %x\n", i, entries, uva);
	return ((int)uva);
}

/* 中文：进入/提交 io_uring：取出提交队列中的 SQE 逐个发起 aio 操作并关联到环，可选阻塞等待本批完成，返回提交数 */
/* EN: Enter/submit an io_uring: take SQEs from the submission queue, start aio operations tied to the ring, optionally block waiting for the batch, returning the submitted count */
static int
uring_enter_sys(unsigned to_submit, int wait, struct sysregs *r)
{
	struct uring_ring *rg = 0;
	struct uring_header *h;
	unsigned avail, submitted = 0;
	int i;

	for (i = 0; i < NURING; i++)
		if (rings[i].r_used && rings[i].r_proc == cur) {
			rg = &rings[i];
			break;
		}
	if (rg == 0)
		return (-EINVAL);
	h = rg->r_hdr;
	if (h->magic != URING_MAGIC)
		return (-EINVAL);
	avail = h->sq_tail - h->sq_head;
	if (to_submit < avail)
		avail = to_submit;
	rg->r_expected = 0;
	rg->r_done = 0;
	while (avail > 0) {
		struct uring_sqe *sqe;
		int token, j;

		if ((h->cq_tail - h->cq_head) >= rg->r_entries) {
			h->sq_dropped += avail;
			break;
		}
		sqe = &rg->r_sq[h->sq_head % rg->r_entries];
		token = aio_submit_sys((int)sqe->sq_fd, (int)sqe->sq_op,
		    sqe->sq_buf, sqe->sq_len, sqe->sq_off);
		if (token > 0) {
			for (j = 0; j < NAIO; j++)
				if (aios[j].a_used &&
				    aios[j].a_token == token) {
					aios[j].a_ring = rg;
					aios[j].a_user_data =
					    sqe->sq_user_data;
					break;
				}
		} else {
			(void)uring_cqe_push(rg, sqe->sq_user_data, token);
		}
		h->sq_head++;
		submitted++;
		avail--;
	}
	if (wait && rg->r_done < submitted) {
		rg->r_expected = (int)submitted;
		rg->r_wait_pid = cur->pid;
		rg->r_wait_result = (int)submitted;
		cur->uring_wait = 1;
		cur->regs = *r;
		cur->state = P_BLOCKED;
		proc_sched();
	}
	return ((int)submitted);
}

/* 中文：拆除 io_uring 环：解除用户映射与内核映射、释放物理页并标记环空闲 */
/* EN: Tear down an io_uring ring: unmap user and kernel views, free the physical page and mark the ring unused */
static int
uring_teardown_sys(void)
{
	struct uring_ring *rg = 0;
	int i;

	for (i = 0; i < NURING; i++)
		if (rings[i].r_used && rings[i].r_proc == cur) {
			rg = &rings[i];
			break;
		}
	if (rg == 0)
		return (-EINVAL);
	user_unmap(cur->pd, rg->r_uva);
	vfs_page_unmap(URING_KVA + (unsigned)i * PAGE_SIZE);
	page_free(rg->r_pa);
	rg->r_used = 0;
	hal_address_space_activate(cur->pd);
	return (0);
}

/* 中文：回收最多 n 个当前进程的私有匿名页：把候选页压缩进 zcomp 池并以 PG_ZCOMP 项替换 PTE，释放原物理页 */
/* EN: Reclaim up to n private anonymous pages of the current process: compress candidates into the zcomp pool, replace their PTEs with PG_ZCOMP entries and free the pages */
static int
zcomp_reclaim(int n)
{
	unsigned *pd;
	int i, j, done = 0;

	if (in_zcomp || cur == 0)
		return (0);
	in_zcomp = 1;
	pd = map_page(SCRATCH_PD, cur->pd);
	for (i = 0; i < 0x3f6 && n > 0; i++) {
		unsigned *pt;
		unsigned pte;

		if ((pd[i] & PG_V) == 0)
			continue;
		pt = map_page(SCRATCH_PT, pd[i] & PG_FRAME);
		for (j = 0; j < (int)(PAGE_SIZE / 4) && n > 0; j++) {
			unsigned va = ((unsigned)i << 22) |
			    ((unsigned)j << 12);
			unsigned pa, slot, len;
			unsigned char *src;

			pte = pt[j];
			if ((pte & PG_V) == 0 || (pte & PG_RW) == 0 ||
			    (pte & PG_u) == 0)
				continue;
			if (pte & PG_COW)
				continue;
			if (vma_find(cur, va) != 0)
				continue;
			pa = pte & PG_FRAME;
			if (page_ref_get(pa) > 1)
				continue;
			src = map_page(SCRATCH_PAGE, pa);
			if (zcomp_store(src, &slot, &len) < 0)
				continue;
			pt[j] = (slot << 12) | PG_ZCOMP | PG_u;
			page_put(pa);
			done++;
			n--;
		}
		hal_address_space_activate(cur->pd);
	}
	in_zcomp = 0;
	return (done);
}

/* 中文：munmap 核心：找到起始地址处的 VMA，逐页解除映射；共享页释放页缓存引用，私有页释放物理页，并收缩或清空 VMA */
/* EN: munmap core: find the VMA at addr, unmap pages; shared pages drop page-cache references, private pages are freed, then shrink or clear the VMA */
static int
do_munmap(unsigned addr, unsigned len)
{
	struct vma *v = vma_find(cur, addr);
	unsigned i;

	if (v == 0 || addr != v->start)
		return (-EINVAL);
	if (len == 0 || len > v->len)
		len = v->len;
	len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	for (i = 0; i < len; i += PAGE_SIZE) {
		unsigned va = addr + i;

		if (v->vp != 0 && (v->flags & MAP_SHARED)) {
			unsigned pa = user_getpa(cur->pd, va);

			user_unmap(cur->pd, va);
			if (pa)
				pagecache_unref_key(v->vid, i / PAGE_SIZE);
		} else {
			/* private/anon page is owned by the process */
			unsigned pa = user_getpa(cur->pd, va);

			user_unmap(cur->pd, va);
			if (pa)
				page_free(pa);
		}
	}
	if (len >= v->len) {
		if (v->vp)
			vn_put(v->vp);
		bzero(v, sizeof(*v));
	} else {
		v->start += len;
		v->len -= len;
		v->off += len;
	}
	return (0);
}

/*
 * msync(addr, len): write a MAP_SHARED file mapping back through the vnode.
 * The ISO9660 root is read-only, so only tmpfs mappings can succeed.
 */
/* 中文：msync 核心：把 MAP_SHARED 文件映射的脏页经 vnode write 写回，仅 tmpfs 可写成功，ISO9660 只读则跳过 */
/* EN: msync core: write back dirty MAP_SHARED file-mapping pages via vnode write; only tmpfs succeeds, read-only ISO9660 is skipped */
static int
do_msync(unsigned addr, unsigned len)
{
	struct vma *v = vma_find(cur, addr);
	unsigned i;

	if (v == 0)
		return (-EINVAL);
	if (v->vp == 0 || !(v->flags & MAP_SHARED))
		return (0);
	if (len == 0 || len > v->len)
		len = v->len;
	len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	for (i = 0; i < len; i += PAGE_SIZE) {
		unsigned pindex = v->off / PAGE_SIZE + i / PAGE_SIZE;
		unsigned char *src;
		unsigned mode = 0, size = 0, n;
		unsigned pa = pagecache_get(v->vp, pindex, &src);

		if (pa == 0)
			continue;
		v->vp->ops->getattr(v->vp, &mode, &size);
		n = 0;
		if (size > pindex * PAGE_SIZE) {
			n = size - pindex * PAGE_SIZE;
			if (n > PAGE_SIZE)
				n = PAGE_SIZE;
		}
		if (n > 0 && v->vp->ops->write != 0)
			(void)v->vp->ops->write(v->vp, pindex * PAGE_SIZE,
			    src, n);
		pagecache_unref(v->vp, pindex);
	}
	return (0);
}

/*
 * Drop all mappings before the address space goes away.  Shared pages belong
 * to the page cache: unmap the PTE (free_address_space() must not free them)
 * and drop the page reference.  Private pages are owned by the process and
 * are left for free_address_space().
 */
/* 中文：地址空间销毁前清理所有映射：共享页解除映射并释放页缓存引用，私有页留给 free_address_space 处理 */
/* EN: Drop all mappings before the address space goes away: unmap shared pages and drop cache references, leaving private pages to free_address_space */
static void
mmap_teardown(struct uproc *p)
{
	int i;
	unsigned k;

	for (i = 0; i < NMAP; i++) {
		struct vma *v = &p->p_vma[i];

		if (!v->used)
			continue;
		if (v->vp != 0 && (v->flags & MAP_SHARED)) {
			for (k = 0; k < v->len; k += PAGE_SIZE) {
				if (user_getpa(p->pd, v->start + k) == 0)
					continue;
				user_unmap(p->pd, v->start + k);
				pagecache_unref_key(v->vid,
				    v->off / PAGE_SIZE + k / PAGE_SIZE);
			}
		}
		if (v->vp)
			vn_put(v->vp);
		bzero(v, sizeof(*v));
	}
	p->p_mmap_base = MMAP_BASE;
}

/* 中文：把 ELF 映像的 PT_LOAD 段按程序头逐页映射到指定页目录，设置保护权限并回写入口地址 */
/* EN: Map the ELF image's PT_LOAD segments page by page into a page directory with proper protections, writing back the entry address */
static void
load_elf(unsigned pdpa, const unsigned char *img, unsigned len, unsigned *entry)
{
	const unsigned *eh = (const unsigned *)img;
	unsigned phoff = eh[7];
	unsigned phnum = *(const unsigned short *)(img + 0x2c);
	unsigned i;

	*entry = eh[6];
	for (i = 0; i < phnum; i++) {
		const unsigned *ph = (const unsigned *)(img + phoff + i * 32);
		unsigned type = ph[0], off = ph[1], vaddr = ph[2];
		unsigned filesz = ph[4], memsz = ph[5];
		unsigned prot = PROT_READ |
		    ((ph[6] & 2) ? PROT_WRITE : 0) |
		    ((ph[6] & 1) ? PROT_EXEC : 0);
		unsigned poff = vaddr & (PAGE_SIZE - 1);
		unsigned base = vaddr - poff;
		unsigned fbase = off - poff;	/* file offset of vaddr's page */
		unsigned n;

		if (type != 1)
			continue;
		/*
		 * A PT_LOAD segment is page-congruent but not necessarily
		 * page-aligned: ld -m elf_i386 gives .data a p_vaddr like
		 * 0x080550e0 (the PE/objcopy path happens to align it, which
		 * is why this only showed up on the Linux build).  Map whole
		 * pages from the page containing p_vaddr and let the file
		 * bytes land at their in-page offset.
		 */
		for (n = 0; n < memsz + poff; n += PAGE_SIZE) {
			unsigned pa = page_alloc();
			char *dst;
			unsigned chunk = memsz + poff - n;
			unsigned pos = fbase + n;	/* file offset of this page */
			unsigned fend = off + filesz;	/* segment's file end */

			if (chunk > PAGE_SIZE)
				chunk = PAGE_SIZE;
			zero_page(pa);
			dst = map_page(SCRATCH_PAGE, pa);
			if (pos < fend) {
				unsigned c = fend - pos;

				if (c > chunk)
					c = chunk;
				bcopy(img + pos, dst, c);
			}
			user_map_prot(pdpa, base + n, pa, prot);
		}
	}
}

/*
 * Lay out argc/argv/envp on a fresh page mapped at USER_ARG_PAGE.
 * Returns the user ESP to start with.
 */
/* 中文：在新用户栈上布局 argc/argv/envp 与指针数组，映射参数页并返回初始 ESP */
/* EN: Lay out argc/argv/envp and pointer arrays on a fresh user stack, map the argument page and return the initial ESP */
static unsigned
build_args(unsigned pdpa, char **argv, int argc, char **envp, int envc)
{
	unsigned pa = page_alloc();
	char *pg;
	unsigned esp = USER_STACK_TOP;
	unsigned aptr[MAXARG], eptr[MAXENV];
	int i, n;

	zero_page(pa);
	pg = map_page(SCRATCH_PAGE, pa);

	for (i = 0; i < envc; i++) {
		n = strlen(envp[i]) + 1;
		esp -= n;
		bcopy(envp[i], pg + (esp - USER_ARG_PAGE), n);
		eptr[i] = esp;
	}
	for (i = 0; i < argc; i++) {
		n = strlen(argv[i]) + 1;
		esp -= n;
		bcopy(argv[i], pg + (esp - USER_ARG_PAGE), n);
		aptr[i] = esp;
	}
	esp &= ~3;

	esp -= 4;
	*(unsigned *)(pg + (esp - USER_ARG_PAGE)) = 0;		/* envp NULL */
	for (i = envc - 1; i >= 0; i--) {
		esp -= 4;
		*(unsigned *)(pg + (esp - USER_ARG_PAGE)) = eptr[i];
	}
	esp -= 4;
	*(unsigned *)(pg + (esp - USER_ARG_PAGE)) = 0;		/* argv NULL */
	for (i = argc - 1; i >= 0; i--) {
		esp -= 4;
		*(unsigned *)(pg + (esp - USER_ARG_PAGE)) = aptr[i];
	}
	esp -= 4;
	*(unsigned *)(pg + (esp - USER_ARG_PAGE)) = argc;

	user_map(pdpa, USER_ARG_PAGE, pa);
	return (esp);
}

/* 中文：从 ELF 映像构建可执行用户映像：创建页目录、加载 ELF 段、压入 argv/envp，返回页目录与入口/栈指针 */
/* EN: Build an executable user image from an ELF blob: create a page directory, load ELF segments, push argv/envp, returning the PD and entry/stack pointers */
static unsigned
build_image(const unsigned char *img, unsigned len, unsigned *entry,
    char **argv, int argc, char **envp, int envc, unsigned *uesp)
{
	unsigned pd = user_pd_create();

	load_elf(pd, img, len, entry);
	/*
	 * The stack is demand-paged too: only the argument page is mapped
	 * here; the first push below USER_STACK_TOP faults in a zero page.
	 * 栈也按需分页：这里只映射参数页；首次向 USER_STACK_TOP 以下压栈时
	 * 由缺页处理分配零页。
	 */
	*uesp = build_args(pd, argv, argc, envp, envc);
	return (pd);
}

/* ------------------------------------------------------------------ ramfs */

/* 中文：安全读取用户字符串到内核缓冲 dst：校验地址已映射并逐字节拷贝，遇 NUL 返回长度，越界或无映射返回 -1 */
/* EN: Safely read a user string into kernel buffer dst: verify pages are mapped and copy bytes, return length at NUL or -1 if invalid */
static int
user_str(unsigned p, char *dst, int max)
{
	int i;

	if (p < USER_LOAD || p >= 0xC0000000 || max <= 1)
		return (-1);
	for (i = 0; i < max - 1; i++) {
		if (user_getpa(cur->pd, (p + i) & ~(PAGE_SIZE - 1)) == 0)
			return (-1);
		dst[i] = ((const char *)p)[i];
		if (dst[i] == 0)
			return (i);
	}
	dst[max - 1] = 0;
	return (-1);
}

/* 中文：从用户地址安全读取一个机器字到 vp，成功返回 0，失败返回 -1 */
/* EN: Safely read one machine word from a user address into vp, returning 0 on success or -1 on failure */
static int
user_word(unsigned p, unsigned *vp)
{

	if (ucopyin((void *)p, vp, sizeof(*vp)) < 0)
		return (-1);
	return (0);
}

/* 中文：在只读 ramfs 表中按名字查找文件，找到返回 struct file 指针，否则 0 */
/* EN: Look up a file by name in the read-only ramfs table, returning its struct file pointer or 0 */
static struct file *
fs_lookup(const char *name)
{
	struct file *f;

	for (f = ramfs; f->f_name; f++)
		if (!strcmp(f->f_name, name) && f->f_data != 0)
			return (f);
	return (0);
}

/* 中文：判断 ISO9660 上是否存在给定路径的文件：已挂载且 iso_lookup 成功返回 1 */
/* EN: Check whether a path exists on the ISO9660 image: returns 1 if mounted and iso_lookup succeeds */
static int
isofile_exists(const char *path)
{
	unsigned lba, size;

	return (iso_mounted() && iso_lookup(path, &lba, &size) == 0);
}

/* 中文：为 ramfs 文件 f 在当前进程分配一个 FD_FILE 描述符并初始化其数据/长度/偏移，返回 fd 号或 -1 */
/* EN: Allocate an FD_FILE descriptor for ramfs file f in the current process and init its data/len/offset, returning the fd or -1 */
static int
fd_alloc(struct file *f)
{
	int i;

	for (i = 3; i < NFD; i++) {
		if (!cur->fds[i].used) {
			cur->fds[i].used = 1;
			cur->fds[i].type = FD_FILE;
			cur->fds[i].d = f->f_data;
			cur->fds[i].len = *f->f_len;
			cur->fds[i].off = 0;
			cur->fds[i].lba = 0;
			cur->fds[i].dsize = 0;
			cur->fds[i].dpos = 0;
			cur->fds[i].pipeidx = -1;
			cur->fds[i].rw = 0;
			cur->fds[i].node = -1;
			return (i);
		}
	}
	return (-1);
}

/* 中文：为 vnode vp 分配一个文件描述符（FD_FILE 或 FD_DIR），设置 rw 与偏移并增加引用，返回 fd 号或 -1 */
/* EN: Allocate a file descriptor for vnode vp (FD_FILE or FD_DIR), set rw and offset and take a reference, returning the fd or -1 */
static int
fd_alloc_vnode(struct vnode *vp, int rw, unsigned off)
{
	int i;

	for (i = 3; i < NFD; i++) {
		if (!cur->fds[i].used) {
			bzero(&cur->fds[i], sizeof(cur->fds[i]));
			cur->fds[i].used = 1;
			cur->fds[i].type = (vp->type == VDIR) ? FD_DIR :
			    FD_FILE;
			cur->fds[i].vp = vp;
			cur->fds[i].rw = rw;
			cur->fds[i].off = off;
			cur->fds[i].pipeidx = -1;
			cur->fds[i].node = -1;
			return (i);
		}
	}
	return (-1);
}

/* 中文：为 ISO9660 目录分配 FD_DIR 描述符，记录其起始 lba 与大小，返回 fd 号或 -1 */
/* EN: Allocate an FD_DIR descriptor for an ISO9660 directory, recording its start lba and size, returning the fd or -1 */
static int
fd_alloc_dir(unsigned lba, unsigned size)
{
	int i;

	for (i = 3; i < NFD; i++) {
		if (!cur->fds[i].used) {
			cur->fds[i].used = 1;
			cur->fds[i].type = FD_DIR;
			cur->fds[i].d = 0;
			cur->fds[i].len = size;
			cur->fds[i].off = 0;
			cur->fds[i].lba = lba;
			cur->fds[i].dsize = size;
			cur->fds[i].dpos = 0;
			cur->fds[i].pipeidx = -1;
			cur->fds[i].rw = 0;
			cur->fds[i].node = -1;
			return (i);
		}
	}
	return (-1);
}



/* ---------------------------------------------------------------- tmpfs */

#define	NTMPFS		32
#define	TMPFS_NAMELEN	32
#define	TMPFS_DATASZ	4096
#define	TMPFS_VA	0xFE600000	/* kernel window, one page per node */

static int	tmpfs_mapped[NTMPFS];

struct tmpfs_node {
	int		used;
	int		isdir;
	char		name[TMPFS_NAMELEN];
	unsigned char	*data;		/* one page per file */
	unsigned	size;
};

static struct tmpfs_node tmpfs[NTMPFS];

/* 中文：按名字查找 tmpfs 节点，找到返回节点指针，否则 0 */
/* EN: Find a tmpfs node by name, returning its pointer or 0 */
static struct tmpfs_node *
tmpfs_find(const char *name)
{
	int i;

	for (i = 0; i < NTMPFS; i++)
		if (tmpfs[i].used && strcmp(tmpfs[i].name, name) == 0)
			return (&tmpfs[i]);
	return (0);
}

/* 中文：按名字创建 tmpfs 文件/目录节点：已存在同名同类则返回它，否则分配槽位，文件则映射一页数据并清零 */
/* EN: Create a tmpfs file/directory node by name: return an existing matching node, else allocate a slot and, for files, map and zero a data page */
static struct tmpfs_node *
tmpfs_create(const char *name, int isdir)
{
	struct tmpfs_node *n;
	int i, len;

	len = strlen(name);
	if (len == 0 || len >= TMPFS_NAMELEN)
		return (0);
	if ((n = tmpfs_find(name)) != 0)
		return (n->isdir == isdir ? n : 0);
	for (i = 0; i < NTMPFS; i++) {
		if (!tmpfs[i].used) {
			n = &tmpfs[i];
			bzero(n, sizeof(*n));
			n->used = 1;
			n->isdir = isdir;
			bcopy(name, n->name, len + 1);
			if (!isdir) {
				unsigned off = i * PAGE_SIZE;

				n->data = (unsigned char *)(TMPFS_VA + off);
				if (!tmpfs_mapped[i]) {
					unsigned pa = page_alloc();

					pmap_map((vm_offset_t)n->data,
					    (vm_offset_t)pa,
					    (vm_offset_t)pa + PAGE_SIZE,
					    VM_PROT_READ | VM_PROT_WRITE);
					tmpfs_mapped[i] = 1;
				}
				bzero(n->data, TMPFS_DATASZ);
			}
			return (n);
		}
	}
	return (0);
}

/* 中文：删除 tmpfs 节点：清 used 标志、大小与数据指针 */
/* EN: Remove a tmpfs node: clear its used flag, size and data pointer */
static void
tmpfs_remove(struct tmpfs_node *n)
{

	n->used = 0;
	n->size = 0;
	n->data = 0;
}

/*
 * "/tmp" is the tmpfs root; "/tmp/name" is a node.  Anything deeper is not
 * supported (one flat level is enough for redirection and scratch files).
 */
/* 中文：解析 /tmp 或 /tmp/name 路径：根路径返回 isdir=1、np=0，单层节点返回对应节点及类型，非法路径返回 -1 */
/* EN: Parse a /tmp or /tmp/name path: the root yields isdir=1 and np=0, a single-level node returns its pointer and type, invalid paths return -1 */
static int
tmpfs_lookup(const char *path, struct tmpfs_node **np, int *isdir)
{
	const char *name;

	if (strcmp(path, "/tmp") == 0 || strcmp(path, "/tmp/") == 0) {
		*np = 0;
		*isdir = 1;
		return (0);
	}
	if (strncmp(path, "/tmp/", 5) != 0)
		return (-1);
	name = path + 5;
	if (strchr(name, '/') != 0)
		return (-1);
	if ((*np = tmpfs_find(name)) == 0)
		return (-1);
	*isdir = (*np)->isdir;
	return (0);
}

/* 中文：判断路径是否位于 tmpfs（/tmp、/tmp/ 或 /tmp/...），是则返回 1 */
/* EN: Check whether a path is under tmpfs (/tmp, /tmp/ or /tmp/...), returning 1 if so */
static int
tmpfs_is_tmp(const char *path)
{

	return (strcmp(path, "/tmp") == 0 || strcmp(path, "/tmp/") == 0 ||
	    strncmp(path, "/tmp/", 5) == 0);
}

/* 中文：为 tmpfs 节点 n 分配 FD_TMPFILE 描述符，设置读写方式、偏移和长度，返回 fd 号或 -1 */
/* EN: Allocate an FD_TMPFILE descriptor for tmpfs node n, set access mode, offset and length, returning the fd or -1 */
static int
fd_alloc_tmpfile(struct tmpfs_node *n, int rw, unsigned off)
{
	int i;

	for (i = 3; i < NFD; i++) {
		if (!cur->fds[i].used) {
			bzero(&cur->fds[i], sizeof(cur->fds[i]));
			cur->fds[i].used = 1;
			cur->fds[i].type = FD_TMPFILE;
			cur->fds[i].node = n - tmpfs;
			cur->fds[i].rw = rw;
			cur->fds[i].off = off;
			cur->fds[i].len = n->size;
			cur->fds[i].pipeidx = -1;
			return (i);
		}
	}
	return (-1);
}

/* 中文：分配一个 FD_TMPDIR 描述符用于读取 tmpfs 根目录，返回 fd 号或 -1 */
/* EN: Allocate an FD_TMPDIR descriptor for reading the tmpfs directory, returning the fd or -1 */
static int
fd_alloc_tmpdir(void)
{
	int i;

	for (i = 3; i < NFD; i++) {
		if (!cur->fds[i].used) {
			bzero(&cur->fds[i], sizeof(cur->fds[i]));
			cur->fds[i].used = 1;
			cur->fds[i].type = FD_TMPDIR;
			cur->fds[i].dpos = 0;
			cur->fds[i].node = -1;
			cur->fds[i].pipeidx = -1;
			return (i);
		}
	}
	return (-1);
}

/* ---------------------------------------------------------------- pipes */

static struct uproc *proc_find(int pid);
static void proc_sched(void);

#define	NPIPE	8
#define	PIPESZ	4096

struct pipe {
	int		used;
	int		readers;
	int		writers;
	unsigned	rpos, wpos, count;
	int		rpid;		/* pid blocked in read */
	int		wpid;		/* pid blocked in write */
	unsigned char	buf[PIPESZ];
};

static struct pipe pipes[NPIPE];

/* 中文：分配一个空闲管道槽位并清零，返回管道指针，无空闲则返回 0 */
/* EN: Allocate and clear a free pipe slot, returning its pointer or 0 if none */
static struct pipe *
pipe_alloc(void)
{
	int i;

	for (i = 0; i < NPIPE; i++) {
		if (!pipes[i].used) {
			bzero(&pipes[i], sizeof(pipes[i]));
			pipes[i].used = 1;
			return (&pipes[i]);
		}
	}
	return (0);
}

/* 中文：在指定进程页目录下在内核缓冲与用户地址 va 之间拷贝 n 字节，临时切换 CR3 后恢复 */
/* EN: Copy n bytes between a kernel buffer and user address va under a given page directory, temporarily switching CR3 then restoring */
static void
pipe_copy(unsigned pd, unsigned va, const void *kbuf, unsigned n, int touser)
{
	unsigned save;

	save = hal_address_space_current();
	hal_address_space_activate(pd);
	if (touser)
		bcopy(kbuf, (void *)va, n);
	else
		bcopy((const void *)va, (void *)kbuf, n);
	hal_address_space_activate(save);
}

/*
 * Hand pending data to a reader blocked inside read().  The copy happens in
 * the writer's context, so the reader's page directory is installed for the
 * duration; the kernel is mapped in both.
 */
/* 中文：把管道中待读数据交给阻塞的读者：在读者页目录上下文把数据拷入其缓冲，处理环形回绕，置结果并唤醒；eof 时返回 0 */
/* EN: Hand pending pipe data to a blocked reader: copy into its buffer under its page directory handling ring wrap, set result and wake; EOF yields 0 */
static void
pipe_wake_reader(struct pipe *pp, int eof)
{
	struct uproc *p;
	unsigned n, first;

	if (pp->rpid == 0)
		return;
	p = proc_find(pp->rpid);
	pp->rpid = 0;
	if (p == 0 || p->state != P_BLOCKED || p->pipeidx != pp - pipes)
		return;
	if (eof) {
		p->regs.eax = 0;
		p->state = P_RUN;
		return;
	}
	n = pp->count;
	if (n > p->pipelen)
		n = p->pipelen;
	first = PIPESZ - pp->rpos;
	if (first > n)
		first = n;
	pipe_copy(p->pd, p->pipeat, pp->buf + pp->rpos, first, 1);
	if (n > first)
		pipe_copy(p->pd, p->pipeat + first, pp->buf, n - first, 1);
	pp->rpos = (pp->rpos + n) % PIPESZ;
	pp->count -= n;
	p->regs.eax = n;
	p->state = P_RUN;
}

/* 中文：把管道数据在写者上下文拷入阻塞读者缓冲，处理环形回绕并置结果唤醒；epipe 时返回 -EPIPE */
/* EN: Copy pipe data into a blocked writer's buffer in the writer's context handling ring wrap, set result and wake; epipe yields -EPIPE */
static void
pipe_wake_writer(struct pipe *pp, int epipe)
{
	struct uproc *p;
	unsigned space, n, first;

	if (pp->wpid == 0)
		return;
	p = proc_find(pp->wpid);
	pp->wpid = 0;
	if (p == 0 || p->state != P_BLOCKED || p->pipeidx != pp - pipes)
		return;
	if (epipe) {
		p->regs.eax = (unsigned)-EPIPE;
		p->state = P_RUN;
		return;
	}
	space = PIPESZ - pp->count;
	n = p->pipelen;
	if (n > space)
		n = space;
	first = PIPESZ - pp->wpos;
	if (first > n)
		first = n;
	pipe_copy(p->pd, p->pipeat, pp->buf + pp->wpos, first, 0);
	if (n > first)
		pipe_copy(p->pd, p->pipeat + first, pp->buf, n - first, 0);
	pp->wpos = (pp->wpos + n) % PIPESZ;
	pp->count += n;
	p->regs.eax = n;
	p->state = P_RUN;
}

/* 中文：关闭文件描述符：管道端减少读/写者计数并按需唤醒对端，释放 vnode 引用，最后标记 fd 空闲 */
/* EN: Close a file descriptor: decrement pipe reader/writer counts and wake peers as needed, drop the vnode reference and mark the fd free */
static void
fd_close(int fd)
{
	struct fd *fp;
	struct pipe *pp;

	if (fd < 0 || fd >= NFD || !cur->fds[fd].used)
		return;
	fp = &cur->fds[fd];
	if (fp->type == FD_PIPE && fp->pipeidx >= 0 &&
	    fp->pipeidx < NPIPE) {
		pp = &pipes[fp->pipeidx];
		if (fp->rw == 1)
			pp->readers--;
		else if (fp->rw == 2)
			pp->writers--;
		if (pp->readers <= 0)
			pipe_wake_writer(pp, 1);
		if (pp->writers <= 0)
			pipe_wake_reader(pp, 1);
		if (pp->readers <= 0 && pp->writers <= 0 &&
		    pp->wpid == 0 && pp->rpid == 0) {
			pp->used = 0;
			fp->pipeidx = -1;
		}
	}
	if (fp->vp != 0) {
		vn_put(fp->vp);
		fp->vp = 0;
	}
	fp->used = 0;
}

/* 中文：为 fd 结构持有的 vnode 增加一次引用计数 */
/* EN: Increment the reference count of the vnode held by an fd structure */
static void
fd_ref(struct fd *fp)
{

	if (fp->vp != 0)
		fp->vp->ref++;
}

/* --------------------------------------------------------------- process */

static int user_ok(unsigned p, unsigned len);
extern void resume_user(unsigned pd, struct sysregs *r);
extern int vfs_absolutize(const char *, const char *, char *, unsigned);

#define	SCRATCH2_PD	0xFE203000
#define	SCRATCH2_PT	0xFE204000
#define	SCRATCH2_PAGE	0xFE205000

/*
 * Deep copy of a user address space: new page directory, new page tables
 * and new pages with the same contents.  The kernel's high mappings are
 * shared, as usual.
 */
/* 中文：深拷贝用户地址空间：新建页目录与页表并逐页复制内容，内核高端映射共享 */
/* EN: Deep-copy a user address space: create new page directory and tables and copy each page, sharing kernel high mappings */
static unsigned
copy_address_space(unsigned srcpd)
{
	unsigned dstpd = user_pd_create();
	unsigned *spd = map_page(SCRATCH_PD, srcpd);
	unsigned *dpd = map_page(SCRATCH2_PD, dstpd);
	int i, j;

	for (i = 0; i < 0x3f6; i++) {
		unsigned *spt, *dpt;
		unsigned dptpa;

		if ((spd[i] & PG_V) == 0)
			continue;
		dptpa = page_alloc();
		spt = map_page(SCRATCH_PT, spd[i] & PG_FRAME);
		dpt = map_page(SCRATCH2_PT, dptpa);
		for (j = 0; j < PAGE_SIZE / 4; j++)
			dpt[j] = 0;
		for (j = 0; j < PAGE_SIZE / 4; j++) {
			unsigned spa, dpa;
			char *s, *d;

			if ((spt[j] & PG_V) == 0)
				continue;
			spa = spt[j] & PG_FRAME;
			dpa = page_alloc();
			s = map_page(SCRATCH_PAGE, spa);
			d = map_page(SCRATCH2_PAGE, dpa);
			bcopy(s, d, PAGE_SIZE);
			dpt[j] = dpa | (spt[j] & 0xfff);
		}
		dpd[i] = dptpa | (spd[i] & 0xfff);
	}
	return (dstpd);
}

/*
 * os.md mechanism 2/3: fork() installs copy-on-write mappings instead of
 * copying every page.  Both parent and child point at the same physical
 * page (PG_COW set, PG_RW clear); the first write in either one takes a
 * page fault and gets a private copy.  Pages that were already read-only
 * (text) are shared read-only as before.
 */
/* 中文：写时复制方式克隆用户地址空间：父子共享物理页并置 PG_COW、清 PG_RW，增加引用计数，首次写入再私有复制 */
/* EN: Clone a user address space copy-on-write: parent and child share pages with PG_COW set and PG_RW cleared, bumping refcounts until first write */
static unsigned
copy_address_space_cow(unsigned srcpd)
{
	unsigned dstpd = user_pd_create();
	unsigned *spd = map_page(SCRATCH_PD, srcpd);
	unsigned *dpd = map_page(SCRATCH2_PD, dstpd);
	int i, j;

	for (i = 0; i < 0x3f6; i++) {
		unsigned *spt, *dpt;
		unsigned dptpa;

		if ((spd[i] & PG_V) == 0)
			continue;
		dptpa = page_alloc();
		spt = map_page(SCRATCH_PT, spd[i] & PG_FRAME);
		dpt = map_page(SCRATCH2_PT, dptpa);
		for (j = 0; j < PAGE_SIZE / 4; j++)
			dpt[j] = 0;
		for (j = 0; j < PAGE_SIZE / 4; j++) {
			unsigned pa;

			if ((spt[j] & PG_V) == 0)
				continue;
			pa = spt[j] & PG_FRAME;
			if (spt[j] & PG_RW) {
				spt[j] &= ~PG_RW;
				spt[j] |= PG_COW;
			}
			dpt[j] = spt[j];
			page_ref_inc(pa);
			cow_shared++;
		}
		dpd[i] = dptpa | (spd[i] & 0xfff);
	}
	return (dstpd);
}

/*
 * Demand-zero heap page: a not-present access below the current brk gets a
 * fresh zero page mapped read/write.  Anything outside [heap_start, brk)
 * is still an invalid access and becomes SIGSEGV.
 * 按需零页：访问当前 brk 以下但尚未映射的地址时，分配并映射一个零页；
 * 堆范围之外的访问仍是非法访问，最终投递 SIGSEGV。
 */
/* 中文：按需零页缺页处理：堆或栈范围内的未映射访问分配一个零页映射并激活地址空间，范围外返回 0 */
/* EN: Handle demand-zero faults: map a fresh zeroed page for unmapped heap/stack accesses and activate the address space, returning 0 outside those ranges */
static int
demand_page(unsigned va)
{
	unsigned page = va & ~(PAGE_SIZE - 1);
	unsigned pa;

	if (cur == 0)
		return (0);
	if (page >= USER_HEAP_START && page < cur->brk_mapped)
		;			/* heap */
	else if (page >= USER_STACK_TOP - USER_STACK_SIZE &&
	    page < USER_STACK_TOP)
		;			/* stack */
	else
		return (0);
	pa = page_alloc();
	zero_page(pa);
	user_map(cur->pd, page, pa);
	hal_address_space_activate(cur->pd);
	return (1);
}

/*
 * Write fault on a COW page: if we are the last user just make it
 * writable, otherwise allocate and copy.  Returns 1 when handled.
 */
/* 中文：用户态缺页入口：非 present 页交给压缩重填/按需零页/mmap 缺页处理，COW 写故障则独占或复制页，返回是否处理成功 */
/* EN: User page-fault entry: route not-present faults to zcomp/demand-zero/mmap handlers and handle COW write faults by taking ownership or copying, returning whether handled */
int
user_page_fault(unsigned va, unsigned err)
{
	unsigned *pt;
	unsigned pti, pa, npa;
	char *s, *d;

	if (cur == 0)
		return (0);
	if ((err & 4) == 0)		/* not a user access */
		return (0);
	if ((err & 1) == 0)		/* not present: compressed/heap/stack/mmap */
		return (zcomp_refault(va) || demand_page(va) ||
		    mmap_fault(va & ~(PAGE_SIZE - 1)));
	if ((err & 7) != 7)		/* present + write + user */
		return (0);
	pti = user_pte_ptr(cur->pd, va & ~(PAGE_SIZE - 1), &pt);
	if (pt == 0 || (pt[pti] & PG_V) == 0 || (pt[pti] & PG_COW) == 0)
		return (0);
	pa = pt[pti] & PG_FRAME;
	cow_faults++;
	if (page_ref_get(pa) <= 1) {
		/* sole owner: keep the page, drop the COW marker */
		pt[pti] = (pt[pti] & ~PG_COW) | PG_RW;
	} else {
		npa = page_alloc();
		s = map_page(SCRATCH_PAGE, pa);
		d = map_page(SCRATCH2_PAGE, npa);
		bcopy(s, d, PAGE_SIZE);
		page_put(pa);
		pt[pti] = npa | PG_V | PG_RW | PG_u;
		cow_copies++;
	}
	hal_address_space_activate(cur->pd);
	return (1);
}

/* 中文：打印 COW 统计信息：共享页数、缺页次数与私有复制次数 */
/* EN: Print copy-on-write statistics: shared pages, fault count and private copy count */
void
cow_print(void)
{

	printf("cow: shared %d faults %d copies %d\n",
	    cow_shared, cow_faults, cow_copies);
}

/* 中文：为进程 p 生成用户态内核执行载体线程（主线程），初始化线程与线程链表指针 */
/* EN: Spawn the main user-mode kernel thread for process p and initialize its thread list pointers */
static struct kthread *
proc_add_thread(struct uproc *p, int tid)
{
	struct kthread *t;

	t = kthread_user_spawn(p, tid);
	if (t == 0)
		return (0);
	p->p_thread = t;
	p->p_threads = t;
	p->p_nthreads = 1;
	return (t);
}

/* 中文：分配一个空闲进程槽并初始化：设置 cwd、mmap 基址及 0/1/2 三个控制台 fd，返回进程指针或 0 */
/* EN: Allocate and init a free process slot: set cwd, mmap base and console fds 0/1/2, returning its pointer or 0 */
static struct uproc *
proc_alloc(void)
{
	int i;

	for (i = 0; i < NPROC; i++)
		if (procs[i].state == P_FREE) {
			int fd;

			bzero(&procs[i], sizeof(procs[i]));
			procs[i].p_cwd[0] = '/';
			procs[i].p_cwd[1] = 0;
			procs[i].p_mmap_base = MMAP_BASE;
			for (fd = 0; fd < 3; fd++) {
				procs[i].fds[fd].used = 1;
				procs[i].fds[fd].type = FD_CONSOLE;
				procs[i].fds[fd].rw = (fd == 0) ? 1 : 2;
				procs[i].fds[fd].pipeidx = -1;
				procs[i].fds[fd].node = -1;
			}
			return (&procs[i]);
		}
	return (0);
}

/*
 * S15: remember a short process name for /dev/ps ("comm").  The kernel does
 * not keep argv around, so the executable's basename is the best we have.
 */
/* 中文：记录进程的可执行文件基名到 p_comm（用于 /dev/ps），内核不保留 argv */
/* EN: Record the executable basename of a path into p_comm for /dev/ps; the kernel does not keep argv */
static void
proc_setcomm(struct uproc *p, const char *path)
{
	const char *base, *s;
	int i;

	base = path;
	for (s = path; *s; s++)
		if (*s == '/')
			base = s + 1;
	bzero(p->p_comm, sizeof(p->p_comm));
	for (i = 0; i < (int)sizeof(p->p_comm) - 1 && base[i] != 0; i++)
		p->p_comm[i] = base[i];
}

/* 中文：按 pid 查找进程：找到返回进程指针，否则 0 */
/* EN: Find a process by pid, returning its pointer or 0 */
static struct uproc *
proc_find(int pid)
{
	int i;

	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_FREE && procs[i].pid == pid)
			return (&procs[i]);
	return (0);
}

/* 中文：统计指定 pid 的子进程数量 */
/* EN: Count the number of child processes of a given pid */
static int
proc_children(int pid)
{
	int i, n = 0;

	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_FREE && procs[i].ppid == pid)
			n++;
	return (n);
}

/*
 * Pick the next runnable process and return to user mode.
 * Never returns.
 */
/* 中文：进程调度器：循环运行内核线程与可运行用户线程（先投递信号再 resume_user），无任务时停机；永不返回 */
/* EN: Process scheduler: loop running kernel threads and runnable user threads (deliver signals then resume_user), halting when idle; never returns */
static void
proc_sched(void)
{
	struct uproc *me = cur;
	struct kthread *mt = cur_thread;
	int i;

	for (;;) {
		/*
		 * S21: kernel threads (softirq, page reclaim, IRP workers)
		 * get their own execution carrier first.
		 */
		kthread_run();
		/*
		 * S25: the scheduler walks threads, not processes.  Each
		 * user thread names its address-space container through
		 * t_proc; run state still lives on the process for now.
		 */
		for (i = 0; i < NKTHREAD; i++) {
			struct kthread *t = &kthreads[i];
			struct uproc *p;
			struct sysregs *regs;
			int main_thread;

			if (!t->t_used || t->t_proc == 0 || t == mt)
				continue;
			p = t->t_proc;
			main_thread = (t->t_tid == p->pid);
			if (main_thread) {
				if (p->state != P_RUN)
					continue;
				regs = &p->regs;
			} else {
				if (p->state == P_ZOMBIE ||
				    t->t_state != KT_RUN)
					continue;
				regs = &t->t_regs;
			}
			cur = p;
			cur_thread = t;
			hal_address_space_activate(p->pd);
			hal_kernel_stack_set(t->t_stack_top);
			sig_deliver(p, regs);
			if (main_thread ? p->state == P_RUN :
			    t->t_state == KT_RUN)
				resume_user(p->pd, regs);
		}
		if (me->state == P_RUN && mt != 0) {
			cur = me;
			cur_thread = mt;
			hal_kernel_stack_set(mt->t_stack_top);
			sig_deliver(me, &me->regs);
			if (me->state == P_RUN)
				resume_user(me->pd, &me->regs);
		}
		/* nothing runnable: let deferred work drain, then halt */
		kthread_run();
		if (kthread_runnable())
			continue;
		printf("\n[no runnable process]\n");
		for (;;)
			hal_cpu_halt();
	}
}

/*
 * os.md mechanism 4, rule 1: the clock interrupt preempts a user thread.
 *
 * The interrupt frame on the shared kernel stack describes the interrupted
 * user context.  We store it in that thread's slot, pick the next runnable
 * thread in round-robin order, load its address space, and rewrite the
 * frame; irq0_entry's iret then returns straight into the new thread.
 * Interrupts are off (interrupt gate) for the whole switch, and we never
 * switch while the interrupted context is in kernel mode: kernel-side
 * deferred work is handled at syscall exit via softirq_dispatch().
 */
int	need_resched;
unsigned sched_switches;

/* 中文：时钟中断抢占处理：保存被中断用户线程的上下文，按轮转选中下一可运行线程并改写中断帧，iret 直接进入新线程 */
/* EN: Clock-tick preemption: save the interrupted user context, round-robin pick the next runnable thread and rewrite the frame so iret enters it */
void
sched_tick(struct sysregs *r)
{
	struct kthread *me = cur_thread;
	struct uproc *mp = cur;
	int start, k;

	if (cur == 0 || me == 0 || r == 0 || !hal_intr_from_user(r))
		return;
	if (me->t_base_ipl >= KTHREAD_NO_PREEMPT)	/* thread opted out of preemption */
		return;

	/* save the interrupted context */
	if (me->t_tid == mp->pid)
		mp->regs = *r;
	else
		me->t_regs = *r;

	if (!need_resched)
		return;
	need_resched = 0;

	start = me - kthreads;
	for (k = 1; k < NKTHREAD; k++) {
		struct kthread *t = &kthreads[(start + k) % NKTHREAD];
		struct uproc *p;
		struct sysregs *regs;
		int main_thread;

		if (!t->t_used || t->t_proc == 0 || t == me)
			continue;
		p = t->t_proc;
		if (p->state == P_FREE || p->state == P_ZOMBIE)
			continue;
		main_thread = (t->t_tid == p->pid);
		if (main_thread) {
			if (p->state != P_RUN)
				continue;
			regs = &p->regs;
		} else {
			if (t->t_state != KT_RUN)
				continue;
			regs = &t->t_regs;
		}
		cur = p;
		cur_thread = t;
		hal_address_space_activate(p->pd);
		hal_kernel_stack_set(t->t_stack_top);
		*r = *regs;
		hal_intr_user_resume(r);
		sched_switches++;
		return;
	}
	/* nobody else runnable: current thread keeps the CPU */
	if (me->t_tid == mp->pid)
		mp->regs = *r;
	else
		me->t_regs = *r;
}

/* 中文：fork 实现：分配子进程、COW 克隆地址空间、继承 fd/管道/工作目录、复制上下文，子进程返回 0，父进程返回子 pid */
/* EN: fork implementation: allocate a child, COW-clone its address space, inherit fds/pipes/cwd, copy context; child returns 0, parent returns child pid */
static int
do_fork(struct sysregs *r)
{
	struct uproc *p;
	unsigned pd;

	if ((p = proc_alloc()) == 0)
		return (-EAGAIN);
	pd = copy_address_space_cow(cur->pd);
	if (pd == 0) {
		p->state = P_FREE;
		return (-ENOMEM);
	}
	hal_address_space_activate(cur->pd);		/* flush the parent's stale writable TLBs */
	p->state = P_RUN;
	p->pid = next_pid++;
	p->ppid = cur->pid;
	p->pd = pd;
	(void)proc_add_thread(p, p->pid);
	p->p_next_tid = p->pid + 100;
	p->brk_mapped = cur->brk_mapped;
	bcopy(cur->p_cwd, p->p_cwd, sizeof(p->p_cwd));
	bcopy(cur->p_comm, p->p_comm, sizeof(p->p_comm));
	bcopy(cur->fds, p->fds, sizeof(p->fds));
	{
		int i;

		for (i = 0; i < NFD; i++)
			if (p->fds[i].used && p->fds[i].vp != 0)
				p->fds[i].vp->ref++;
		for (i = 0; i < NFD; i++)
			if (p->fds[i].used && p->fds[i].type == FD_PIPE &&
			    p->fds[i].pipeidx >= 0) {
				if (p->fds[i].rw == 1)
					pipes[p->fds[i].pipeidx].readers++;
				else if (p->fds[i].rw == 2)
					pipes[p->fds[i].pipeidx].writers++;
			}
	}
	p->regs = *r;
	p->regs.eax = 0;		/* the child returns 0 from fork() */

	return (p->pid);
}

/* 中文：vfork 实现：用 COW 私有拷贝共享地址空间，父进程阻塞直到子进程 exec/exit，子进程返回 0 */
/* EN: vfork implementation: use a COW private copy of the shared address space, block the parent until the child execs/exits, child returns 0 */
static int
do_vfork(struct sysregs *r)
{
	struct uproc *p;
	unsigned pd;

	if ((p = proc_alloc()) == 0)
		return (-EAGAIN);
	/*
	 * A strict vfork shares the address space, but our userland has no
	 * way to protect the parent's stack frame while the child runs (the
	 * child's own libc calls would clobber it).  A private copy plus the
	 * "parent waits for exec/exit" rule is a safe superset.  The private
	 * copy is COW, so vfork is cheap until either side writes.
	 */
	pd = copy_address_space_cow(cur->pd);
	if (pd == 0) {
		p->state = P_FREE;
		return (-ENOMEM);
	}
	hal_address_space_activate(cur->pd);
	p->state = P_RUN;
	p->pid = next_pid++;
	p->ppid = cur->pid;
	p->pd = pd;
	(void)proc_add_thread(p, p->pid);
	p->p_next_tid = p->pid + 100;
	p->brk_mapped = cur->brk_mapped;
	bcopy(cur->p_cwd, p->p_cwd, sizeof(p->p_cwd));
	bcopy(cur->p_comm, p->p_comm, sizeof(p->p_comm));
	bcopy(cur->fds, p->fds, sizeof(p->fds));
	p->regs = *r;
	p->regs.eax = 0;
	p->vfork_child = 1;

	/* the parent blocks until the child execs or exits */
	cur->regs = *r;
	cur->state = P_BLOCKED;
	cur->vfork_wait = 1;
	proc_sched();
	return (0);
}

/* 中文：进程退出：唤醒 vfork/wait4 等待者并写状态，关闭所有 fd、释放线程与 mmap 及地址空间，置为僵尸或空闲 */
/* EN: Process exit: wake vfork/wait4 waiters with status, close all fds, free thread/mmap/address space, then mark zombie or free */
static void
proc_exit(int code)
{
	struct uproc *p;
	int i, reaped = 0;

	for (i = 0; i < NPROC; i++) {
		p = &procs[i];
		if (p->state != P_BLOCKED || p->pid != cur->ppid)
			continue;
		if (p->vfork_wait) {
			p->vfork_wait = 0;
			p->regs.eax = cur->pid;
			p->state = P_RUN;
			continue;
		}
		if (p->wait_pid != 0 && p->wait_pid != cur->pid)
			continue;
		if (p->wait_status != 0 && user_ok(p->wait_status, 4)) {
			unsigned save = cur->pd;

			hal_address_space_activate(p->pd);
			*(int *)p->wait_status = cur->p_termsig ?
			    (cur->p_termsig & 0x7f) : ((code & 0xff) << 8);
			hal_address_space_activate(save);
		}
		p->regs.eax = cur->pid;
		p->state = P_RUN;
		reaped = 1;		/* the parent's wait4 gets the status */
	}

	{
		int fd;

		for (fd = 0; fd < NFD; fd++)
			if (cur->fds[fd].used)
				fd_close(fd);
	}
	/* release the process's main thread slot */
	if (cur->p_thread != 0) {
		kthread_user_free(cur->p_thread);
		cur->p_thread = 0;
	}
	/*
	 * S17: release mmap bookkeeping first.  Shared page-cache pages must
	 * be unmapped here so free_address_space() does not return them to the
	 * page pool; private pages are left for it to free.
	 */
	mmap_teardown(cur);
	/*
	 * S10.1: give the address space back.  Switch to the kernel page
	 * directory first - the user PD is about to be freed and we are
	 * still running on its kernel-stack mapping (0x3f6 is shared, so the
	 * stack itself stays valid on IdlePTD).
	 */
	if (cur->pd != 0) {
		unsigned save = cur->pd;

		/* CR3 wants the *physical* address (IdlePTD), not pm_pdir's VA */
		hal_address_space_activate((unsigned)IdlePTD);
		free_address_space(save);
		cur->pd = 0;
	}
	cur->exitcode = code;
	cur->state = reaped ? P_FREE : P_ZOMBIE;
	if (cur->ppid == 0) {
		printf("\n[user exit %d]\n", code);
		for (;;)
			hal_cpu_halt();
	}
	proc_sched();
}

/*
 * A user-mode exception (SIGSEGV/SIGFPE/SIGILL/...).  os.md mechanism 1:
 * the application dies, the kernel survives.  The faulting process is
 * terminated with the signal recorded in p_termsig so wait4() reports
 * WIFSIGNALED; the scheduler then runs another thread.
 */
/* 中文：用户态异常（SIGSEGV 等）处理：记录 p_termsig、终止该进程所有线程并调用 proc_exit，保证应用死而内核存活 */
/* EN: Handle a user-mode exception: record p_termsig, terminate all threads and call proc_exit so the app dies but the kernel survives */
void
user_fault(int sig, unsigned eip, unsigned va)
{
	int i;

	if (cur == 0) {
		printf("\nfault in kernel context: sig %d eip %x va %x\n",
		    sig, eip, va);
		for (;;)
			hal_cpu_halt();
	}
	printf("\nuser fault: pid %d sig %d eip %x va %x\n",
	    cur->pid, sig, eip, va);
	cur->p_termsig = sig;
	/* a thread-fatal signal ends the whole process (os.md 9.2/9.4) */
	for (i = 0; i < NKTHREAD; i++) {
		struct kthread *t = &kthreads[i];

		if (t->t_used && t->t_proc == cur && t->t_tid != cur->pid) {
			t->t_state = KT_ZOMBIE;
			t->t_exitcode = 128 + sig;
		}
	}
	proc_exit(0);
}

/* ---------------------------------------------------------------- signals */

static int	console_fg_pgrp = 1;

struct sigframe {
	unsigned	magic;
	unsigned	sig;
	unsigned	mask;
	unsigned	eax, ebx, ecx, edx, esi, edi, ebp;
	unsigned	ds, es;
	unsigned	eip, cs, eflags, uesp, ss;
};

#define	SIGFRAME_MAGIC	0x53494746	/* "SIGF" */
#define	SIGFRAME_SIZE	(sizeof(struct sigframe))
#define	SIGTRAMP		8	/* movl $103,%eax ; int $0x80 ; hlt */

/* 0 = ignore, 1 = terminate, 2 = stop */
/* 中文：返回信号的默认动作：忽略、终止(1)或停止(2)，SIGCHLD/SIGCONT 等忽略，停止类信号返回 2 */
/* EN: Return a signal's default action: ignore, terminate (1) or stop (2); SIGCHLD/SIGCONT are ignored, stop signals return 2 */
static int
sig_default(int sig)
{

	switch (sig) {
	case SIGCHLD: case SIGCONT: case 16: case 23: case 28: case 29:
		return (0);
	case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
		return (2);
	default:
		return (1);
	}
}

/* 中文：向进程 p 发送信号：SIGCONT 恢复停止进程，否则置挂起位；阻塞进程被置可运行并收到 -EINTR */
/* EN: Send a signal to process p: SIGCONT resumes a stopped process, else set the pending bit; a blocked process becomes runnable with -EINTR */
static void
sig_send(struct uproc *p, int sig)
{

	if (p == 0 || p->state == P_FREE || sig <= 0 || sig >= NSIG)
		return;
	if (sig == SIGCONT) {
		if (p->state == P_STOPPED) {
			p->state = P_RUN;
			p->p_stopsig = 0;
			p->p_cont = 1;
		}
		return;
	}
	p->p_sig |= SIG_TO_MASK(sig);
	/* a blocked process gets EINTR and is made runnable again */
	if (p->state == P_BLOCKED) {
		p->state = P_RUN;
		p->regs.eax = (unsigned)-EINTR;
	}
}

/* 中文：向进程组 pgrp 的所有进程发送信号 */
/* EN: Send a signal to every process in process group pgrp */
static void
sig_send_pgrp(int pgrp, int sig)
{
	int i;

	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_FREE && procs[i].p_pgrp == pgrp)
			sig_send(&procs[i], sig);
}

/* 中文：按 pid 向单个进程发送信号，找不到则忽略 */
/* EN: Send a signal to a single process by pid, ignoring it if not found */
static void
sig_send_pid(int pid, int sig)
{
	struct uproc *p = proc_find(pid);

	if (p != 0)
		sig_send(p, sig);
}

/* push a sigframe + trampoline on the user stack and point the frame at it */
/* 中文：在用户栈上压入 sigframe 与返回跳板（sigreturn 系统调用），把上下文改为信号处理入口；栈无效返回 -1 */
/* EN: Push a sigframe and sigreturn trampoline on the user stack and redirect context to the handler; returns -1 for a bad stack */
static int
sig_setup(struct uproc *p, struct sysregs *r, int sig)
{
	static const unsigned char tramp[8] = {
		0xb8, 0x67, 0x00, 0x00, 0x00,	/* movl $103,%eax	*/
		0xcd, 0x80,			/* int $0x80		*/
		0xf4				/* hlt			*/
	};
	struct sigframe *fp;
	unsigned sp = r->uesp & ~3u;
	unsigned stub;
	int i;

	sp -= SIGFRAME_SIZE;
	if (sp < 0x08000000 || !user_ok(sp, SIGFRAME_SIZE))
		return (-1);
	fp = (struct sigframe *)sp;
	fp->magic = SIGFRAME_MAGIC;
	fp->sig = sig;
	fp->mask = p->p_sigmask;
	fp->eax = r->eax; fp->ebx = r->ebx; fp->ecx = r->ecx; fp->edx = r->edx;
	fp->esi = r->esi; fp->edi = r->edi; fp->ebp = r->ebp;
	fp->ds = r->ds; fp->es = r->es;
	fp->eip = r->eip; fp->cs = r->cs; fp->eflags = r->eflags;
	fp->uesp = r->uesp; fp->ss = r->ss;

	sp -= SIGTRAMP;
	stub = sp;
	{
		unsigned char *d = (unsigned char *)sp;

		for (i = 0; i < SIGTRAMP; i++)
			d[i] = tramp[i];
	}
	sp -= 4;
	*(unsigned *)sp = (unsigned)sig;	/* handler argument */
	sp -= 4;
	*(unsigned *)sp = stub;			/* return address -> trampoline */

	r->uesp = sp;
	r->eip = p->p_sighand[sig];
	r->eax = (unsigned)sig;
	/* the handler runs with the signal blocked (no SA_NODEFER) */
	p->p_sigmask |= SIG_TO_MASK(sig);
	return (0);
}

/* 中文：停止进程：置 P_STOPPED 与停止信号，并唤醒带 WUNTRACED 等待的父进程写入停止状态 */
/* EN: Stop a process: set P_STOPPED and the stop signal, then wake a WUNTRACED-waiting parent with the stop status */
static void
sig_stop(struct uproc *p, int sig)
{
	struct uproc *pp;

	p->state = P_STOPPED;
	p->p_stopsig = sig;
	pp = proc_find(p->ppid);
	if (pp != 0 && pp->state == P_BLOCKED && pp->wait_status != 0 &&
	    (pp->wait_options & WUNTRACED) &&
	    (pp->wait_pid == 0 || pp->wait_pid == p->pid) &&
	    user_ok(pp->wait_status, 4)) {
		hal_address_space_activate(pp->pd);
		*(int *)pp->wait_status = ((sig & 0xff) << 8) | 0x7f;
		hal_address_space_activate(cur->pd);
		pp->regs.eax = p->pid;
		pp->state = P_RUN;
	}
}

/*
 * Deliver at most one signal per return to user mode.  The caller must run
 * with the target process's page directory loaded.
 */
/* 中文：返回用户态前投递至多一个信号：处理 SIGCONT/忽略/捕获/默认动作，默认终止则退出或置僵尸；调用前须已加载目标页目录 */
/* EN: Deliver at most one pending signal before returning to user mode: handle SIGCONT/ignore/catch/default, terminating or zombifying as needed; caller must have the target PD loaded */
static void
sig_deliver(struct uproc *p, struct sysregs *r)
{
	int sig;
	unsigned pend, mask;
	struct kthread *t = 0;

	if (cur_thread != 0 && cur_thread->t_proc == p)
		t = cur_thread;
	else if (p->p_thread != 0)
		t = p->p_thread;
	mask = (t != 0) ? t->t_sigmask : p->p_sigmask;
	for (;;) {
		pend = (p->p_sig | (t != 0 ? t->t_sig : 0)) & ~mask;
		pend |= (p->p_sig | (t != 0 ? t->t_sig : 0)) &
		    (SIG_TO_MASK(SIGKILL) | SIG_TO_MASK(SIGSTOP));
		if (pend == 0)
			return;
		for (sig = 1; sig < NSIG; sig++)
			if (pend & SIG_TO_MASK(sig))
				break;
		if (sig >= NSIG)
			return;
		p->p_sig &= ~SIG_TO_MASK(sig);
		if (t != 0)
			t->t_sig &= ~SIG_TO_MASK(sig);

		if (sig == SIGCONT) {
			if (p->p_stopsig != 0) {
				p->p_stopsig = 0;
				p->state = P_RUN;
			}
			continue;
		}
		if (p->p_sigignore & SIG_TO_MASK(sig))
			continue;
		if ((p->p_sigcatch & SIG_TO_MASK(sig)) && r != 0 &&
		    sig_setup(p, r, sig) == 0)
			return;
		if (sig_default(sig) == 2) {
			sig_stop(p, sig);
			return;
		}
		if (sig_default(sig) == 0)
			continue;
		/* terminate */
		p->p_termsig = sig;
		if (p == cur)
			proc_exit(sig);		/* never returns */
		p->exitcode = sig;
		p->state = P_ZOMBIE;
		return;
	}
}
/* 中文：wait4 实现：回收指定（或任意）僵尸/停止子进程并写状态，无子进程返回 ECHILD，WNOHANG 立即返回，否则阻塞调度 */
/* EN: wait4 implementation: reap a specific (or any) zombie/stopped child and write status; ECHILD if none, WNOHANG returns immediately, else block and schedule */
static int
do_wait4(int want, unsigned statusp, int options, struct sysregs *r)
{
	struct uproc *p;
	int i, pid;

	for (i = 0; i < NPROC; i++) {
		p = &procs[i];
		if (p->state == P_FREE || p->ppid != cur->pid)
			continue;
		if (want > 0 && p->pid != want)
			continue;
		pid = p->pid;
		if (p->state == P_ZOMBIE) {
			if (statusp != 0 && user_ok(statusp, 4))
				*(int *)statusp = p->p_termsig ?
				    (p->p_termsig & 0x7f) :
				    ((p->exitcode & 0xff) << 8);
			p->state = P_FREE;
			r->eax = pid;
			return (0);
		}
		if (p->state == P_STOPPED && (options & WUNTRACED)) {
			if (statusp != 0 && user_ok(statusp, 4))
				*(int *)statusp =
				    ((p->p_stopsig & 0xff) << 8) | 0x7f;
			r->eax = pid;
			return (0);
		}
	}
	if (proc_children(cur->pid) == 0) {
		r->eax = (unsigned)-ECHILD;
		return (0);
	}
	if (options & WNOHANG) {
		r->eax = 0;
		return (0);
	}
	cur->regs = *r;
	cur->wait_pid = (want > 0) ? want : 0;
	cur->wait_status = statusp;
	cur->wait_options = options;
	cur->state = P_BLOCKED;
	proc_sched();
	r->eax = 0;
	return (0);
}


/* ---- S13: render the process table for the /dev/ps pseudo file ---- */

/* 中文：把字符串 s 追加到缓冲区 buf 的偏移 o 处（留一字节余量），返回新偏移 */
/* EN: Append string s to buffer buf at offset o (leaving one byte of slack), returning the new offset */
static unsigned
ps_str(char *buf, unsigned o, unsigned len, const char *s)
{

	while (*s && o + 1 < len)
		buf[o++] = *s++;
	return (o);
}

/* 中文：把无符号数 v 以十进制追加到缓冲区 buf 的偏移 o 处，返回新偏移 */
/* EN: Append unsigned value v in decimal to buffer buf at offset o, returning the new offset */
static unsigned
ps_num(char *buf, unsigned o, unsigned len, unsigned v)
{
	char num[12];
	int n = 0;

	if (v == 0)
		num[n++] = '0';
	while (v > 0) {
		num[n++] = '0' + (v % 10);
		v /= 10;
	}
	while (n > 0 && o + 1 < len)
		buf[o++] = num[--n];
	return (o);
}

/* 中文：为 /dev/ps 生成进程表文本：逐进程列出 pid、ppid、pgrp、状态与 comm，NUL 结尾并返回长度 */
/* EN: Render the process table text for /dev/ps: list pid, ppid, pgrp, state and comm per process, NUL-terminate and return the length */
static int
proc_list_text(char *buf, unsigned len)
{
	unsigned o = 0;
	int i;

	for (i = 0; i < NPROC; i++) {
		const char *st;

		if (procs[i].state == P_FREE)
			continue;
		switch (procs[i].state) {
		case P_RUN:	st = "RUN"; break;
		case P_BLOCKED:	st = "BLOCK"; break;
		case P_ZOMBIE:	st = "ZOMB"; break;
		case P_STOPPED:	st = "STOP"; break;
		default:	st = "?"; break;
		}
		o = ps_num(buf, o, len, (unsigned)procs[i].pid);
		o = ps_str(buf, o, len, " ");
		o = ps_num(buf, o, len, (unsigned)procs[i].ppid);
		o = ps_str(buf, o, len, " ");
		o = ps_num(buf, o, len, (unsigned)procs[i].p_pgrp);
		o = ps_str(buf, o, len, " ");
		o = ps_str(buf, o, len, st);
		o = ps_str(buf, o, len, " ");
		o = ps_str(buf, o, len, procs[i].p_comm);
		o = ps_str(buf, o, len, "\n");
	}
	if (o < len)
		buf[o] = 0;
	return ((int)o);
}

/* --------------------------------------------------------------- syscalls */
/* Absolutize a path argument once, here in the syscall layer. */
/* 中文：在系统调用层把用户路径参数 uptr 读入 rel 并用当前 cwd 绝对化到 buf，读取失败返回 -EFAULT */
/* EN: Read a user path argument at uptr into rel and absolutize it against the current cwd into buf; returns -EFAULT on read failure */
static int
path_arg(unsigned uptr, char *buf, unsigned len)
{
	char rel[128];

	if (user_str(uptr, rel, sizeof(rel)) < 0)
		return (-EFAULT);
	return (vfs_absolutize(cur->p_cwd, rel, buf, len));
}



/* 中文：校验用户缓冲 [p,p+len) 可读：逐页检查映射，必要时补页/COW 私有化，全部有效返回 1，否则 0 */
/* EN: Validate that user buffer [p,p+len) is readable: check each page's mapping, demand-fault/COW as needed, return 1 if all valid else 0 */
static int
user_ok(unsigned p, unsigned len)
{
	unsigned a;

	if (len == 0)
		return (1);
	if (p < USER_LOAD || p >= 0xC0000000)
		return (0);
	if (len > 0xC0000000 - p)
		return (0);
	/*
	 * Walk the page tables instead of trusting the address range: a bad
	 * pointer inside the user window must return EFAULT, not fault in the
	 * kernel (os.md mechanism 1, access_ok + copy_from_user).
	 */
	for (a = p; a < p + len; a = (a + PAGE_SIZE) & ~(PAGE_SIZE - 1)) {
		unsigned *pt;
		unsigned pti;

		pti = user_pte_ptr(cur->pd, a, &pt);
		if (pt == 0 || (pt[pti] & PG_V) == 0) {
			/*
			 * A legal heap address below brk is demand-paged here,
			 * so syscall buffers on untouched heap work without a
			 * user fault; anything else is EFAULT.
			 * brk 以下的合法堆地址在此按需映射，系统调用缓冲区
			 * 可直接使用未触碰的堆页；其它地址仍返回 EFAULT。
			 */
			if (!zcomp_refault(a) && !demand_page(a) &&
			    !mmap_fault(a & ~(PAGE_SIZE - 1)))
				return (0);
			pti = user_pte_ptr(cur->pd, a, &pt);
			if (pt == 0 || (pt[pti] & PG_V) == 0)
				return (0);
		}
		if ((pt[pti] & PG_COW) != 0) {
			/* output paths write here: give the page a private copy */
			if (!user_page_fault(a, 7))
				return (0);
			pti = user_pte_ptr(cur->pd, a, &pt);
			if (pt == 0 || (pt[pti] & PG_V) == 0)
				return (0);
		}
	}
	return (1);
}

/* Like user_ok(), but the kernel is going to write: the PTE must be RW. */
/* 中文：校验用户缓冲可写：在 user_ok 基础上额外要求每页 PTE 具 PG_RW，全部满足返回 1 */
/* EN: Validate that a user buffer is writable: on top of user_ok, additionally require PG_RW on every page's PTE, returning 1 if all satisfied */
static int
user_ok_w(unsigned p, unsigned len)
{
	unsigned a;

	if (!user_ok(p, len))
		return (0);
	for (a = p; a < p + len; a = (a + PAGE_SIZE) & ~(PAGE_SIZE - 1)) {
		unsigned *pt;
		unsigned pti = user_pte_ptr(cur->pd, a, &pt);

		if (pt == 0 || (pt[pti] & PG_RW) == 0)
			return (0);
	}
	return (1);
}

/*
 * Page-validating user copies (os.md mechanism 1).  Named ucopy* to avoid
 * the 4.5BSD locore.s copyin/copyout assembly, which uses pcb_onfault and
 * is not wired into this port; the contract is the same: 0 or -EFAULT.
 */
/* 中文：校验用户地址后从用户缓冲拷贝 len 字节到内核 kaddr，成功返回 0，否则 -EFAULT */
/* EN: After validating the user address, copy len bytes from user buffer into kernel kaddr; returns 0 or -EFAULT */
static int
ucopyin(void *udaddr, void *kaddr, unsigned len)
{

	if (!user_ok((unsigned)udaddr, len))
		return (-EFAULT);
	bcopy(udaddr, kaddr, len);
	return (0);
}

/* 中文：校验用户地址可写后从内核 kaddr 拷贝 len 字节到用户缓冲，成功返回 0，否则 -EFAULT */
/* EN: After validating the user address is writable, copy len bytes from kernel kaddr into the user buffer; returns 0 or -EFAULT */
static int
ucopyout(void *kaddr, void *udaddr, unsigned len)
{

	if (!user_ok_w((unsigned)udaddr, len))
		return (-EFAULT);
	bcopy(kaddr, udaddr, len);
	return (0);
}

/*
 * S16: wall clock.  The kernel has no clock interrupt handler wired up, so
 * read the CMOS RTC directly instead: registers are indexed through port
 * 0x70 and read from 0x71 by rtcin() in locore.s.  The RTC holds local time
 * (the host's time zone); we hand it out as UTC until a timezone table
 * exists.  Sub-second resolution is not available, hence tv_usec = 0.
 */
extern unsigned char rtcin(int);

/* 中文：把 CMOS RTC 的 BCD 编码字节 v 转换为十进制整数 */
/* EN: Convert a BCD-encoded CMOS RTC byte v into a decimal integer */
static int
rtc_bcd(int v)
{

	return ((v & 0xf) + (v >> 4) * 10);
}

/* 中文：把年月日 (y,m,d) 换算为自 1970-01-01 起的天数，考虑闰年 */
/* EN: Convert a date (y,m,d) into days since 1970-01-01, accounting for leap years */
static long
rtc_days(int y, int m, int d)
{
	static const int mdays[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243,
	    273, 304, 334 };
	long days = 0;
	int i, leap;

	for (i = 1970; i < y; i++) {
		leap = (i % 4 == 0 && (i % 100 != 0 || i % 400 == 0));
		days += 365 + leap;
	}
	days += mdays[m - 1];
	if (m > 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)))
		days++;
	days += d - 1;
	return (days);
}

/* 中文：直接从 CMOS RTC 读取当前时间并换算为 Unix 纪元秒数，处理 12/24 小时与 BCD/二进制格式，非法日期返回 0 */
/* EN: Read the current time directly from the CMOS RTC and convert to Unix epoch seconds, handling 12/24-hour and BCD/binary formats; invalid dates return 0 */
static long
rtc_epoch(void)
{
	int sec, min, hour, day, mon, year, i;
	unsigned char sb;

	/* wait for the update-in-progress flag to clear */
	for (i = 0; i < 1000000; i++)
		if ((rtcin(0x0a) & 0x80) == 0)
			break;
	sb = rtcin(0x0b);
	sec = rtcin(0x00);
	min = rtcin(0x02);
	hour = rtcin(0x04);
	day = rtcin(0x07);
	mon = rtcin(0x08);
	year = rtcin(0x09);
	if ((sb & 0x02) == 0 && (hour & 0x80) != 0)	/* 12-hour clock, PM */
		hour = (hour & 0x7f) + 12;
	if ((sb & 0x04) == 0) {				/* BCD, not binary */
		sec = rtc_bcd(sec);
		min = rtc_bcd(min);
		hour = rtc_bcd(hour);
		day = rtc_bcd(day);
		mon = rtc_bcd(mon);
		year = rtc_bcd(year);
	}
	if (mon < 1 || mon > 12 || day < 1 || day > 31)
		return (0);
	return (rtc_days(year + 2000, mon, day) * 86400L +
	    hour * 3600L + min * 60L + sec);
}

/*
 * Fill a 4.4BSD i386 `struct stat` (96 bytes):
 *	0 dev, 4 ino, 8 mode(16), 10 nlink(16), 12 uid, 16 gid, 20 rdev,
 *	24 atimespec, 32 mtimespec, 40 ctimespec,
 *	48 size(64), 56 blocks(64), 64 blksize, 68 flags, 72 gen,
 *	76 lspare, 80 qspare[2]
 */
#define	STAT_SIZE	96

/* 中文：填充 4.4BSD i386 struct stat（96 字节）：清零后置 dev/ino/mode/nlink/size/blksize 等字段 */
/* EN: Fill a 4.4BSD i386 struct stat (96 bytes): zero it then set dev/ino/mode/nlink/size/blksize fields */
static void
fill_stat(unsigned mode, unsigned size, unsigned *sb)
{
	char *p = (char *)sb;
	int i;

	for (i = 0; i < STAT_SIZE; i++)
		p[i] = 0;
	*(unsigned *)(p + 0) = 0;		/* st_dev */
	*(unsigned *)(p + 4) = 1;		/* st_ino */
	*(unsigned short *)(p + 8) = (unsigned short)mode;
	*(unsigned short *)(p + 10) = 1;	/* st_nlink */
	*(unsigned *)(p + 48) = size;		/* st_size */
	*(unsigned *)(p + 52) = 0;
	*(unsigned *)(p + 64) = 4096;		/* st_blksize */
}

/* 中文：exec 核心：经 VFS 读取可执行文件到内核窗口，从用户态复制 argv/envp，构建新映像并切换地址空间与上下文；vfork 子进程唤醒父进程 */
/* EN: exec core: read the executable via VFS into a kernel window, copy argv/envp from user, build a new image and switch address space and context; a vfork child wakes its parent */
static int
exec_image(const char *path, unsigned uargv, unsigned uenvp, struct sysregs *r)
{
	struct file *f;
	const unsigned char *img;
	unsigned len;
	unsigned pd, entry, esp;
	char *argv[MAXARG], *envp[MAXENV];
	static char argbuf[1024];
	int argc = 0, envc = 0;
	unsigned p, n;
	char *b = argbuf;

	/*
	 * Prefer the ISO9660 root: S4 moved /sbin/init and /bin/sh onto the
	 * CD.  The embedded ramfs images stay as a fallback for a kernel
	 * booted without a disc.
	 */
	f = 0;
	{
		struct vnode *vp = 0;
		unsigned char *buf;
		int rv;

		if (vfs_namei(path, &vp) < 0 || vp->type != VREG)
			return (-ENOENT);
		len = vp->size;
		buf = cd_map(len);
		if (buf == 0) {
			vn_put(vp);
			return (-ENOMEM);
		}
		rv = vp->ops->read(vp, 0, buf, len);
		if (rv < 0 || (unsigned)rv != len) {
			vn_put(vp);
			return (-EIO);
		}
		printf("execve %s: via vnode (%d bytes)\n", path, len);
		img = buf;
		vn_put(vp);
	}

	if (uargv != 0) {
		while (argc < MAXARG && user_word(uargv, &p) == 0 && p != 0) {
			n = user_str(p, b, 128);
			if ((int)n < 0)
				return (-EFAULT);
			argv[argc++] = b;
			b += n + 1;
			uargv += 4;
		}
	}
	envp[envc++] = "PATH=/bin:/sbin";

	pd = build_image(img, len, &entry, argv, argc, envp, envc, &esp);
	/* S17: the old address space is replaced, drop its mmap references */
	mmap_teardown(cur);
	cur->pd = pd;
	cur->brk_mapped = USER_HEAP_START;
	printf("execve %s: entry %x esp %x pd %x argc %d\n",
	    path, entry, esp, pd, argc);
	hal_address_space_activate(pd);
	r->eip = entry;
	r->uesp = esp;
	r->eax = 0;
	if (cur->vfork_child) {
		struct uproc *pp = proc_find(cur->ppid);

		cur->vfork_child = 0;
		if (pp != 0 && pp->state == P_BLOCKED && pp->vfork_wait) {
			pp->vfork_wait = 0;
			pp->regs.eax = cur->pid;
			pp->state = P_RUN;
			cur->regs = *r;
			cur->state = P_RUN;
			cur = pp;
			cur_thread = pp->p_thread;
			if (cur_thread != 0)
				hal_kernel_stack_set(cur_thread->t_stack_top);
			resume_user(pp->pd, &pp->regs);
		}
	}
	return (0);
}


/*
 * Canonical console input for fd 0: echo, erase (BS/DEL), kill (^U),
 * word-erase (^W) and EOF (^D on an empty line).
 */
/* 中文：控制台 fd 0 的规范输入：回显并处理退格、^U 清行、^W 清词、^D EOF，以及 ^C/^Z/^\ 向前台进程组发信号 */
/* EN: Canonical console input for fd 0: echo with backspace, ^U kill-line, ^W kill-word, ^D EOF and ^C/^Z/^\ signals to the foreground pgrp */
static int
console_read(char *buf, unsigned len)
{
	unsigned i = 0;
	int c;

	while (i < len - 1) {
		c = cngetc();
		if (c == 0)
			continue;
		if (c == '\r' || c == '\n') {
			cnputc('\n');
			buf[i++] = '\n';
			break;
		}
		if (c == '\b' || c == 0x7f) {
			if (i > 0) {
				i--;
				cnputc('\b');
				cnputc(' ');
				cnputc('\b');
			}
			continue;
		}
		if (c == 0x15) {		/* ^U: kill line */
			while (i > 0) {
				i--;
				cnputc('\b');
				cnputc(' ');
				cnputc('\b');
			}
			continue;
		}
		if (c == 0x17) {		/* ^W: kill word */
			while (i > 0 && (buf[i - 1] == ' ' || buf[i - 1] == '\t')) {
				i--;
				cnputc('\b');
				cnputc(' ');
				cnputc('\b');
			}
			while (i > 0 && buf[i - 1] != ' ' && buf[i - 1] != '\t') {
				i--;
				cnputc('\b');
				cnputc(' ');
				cnputc('\b');
			}
			continue;
		}
		if (c == 0x04)			/* ^D: EOF on an empty line */
			break;
		if (c == 0x03) {		/* ^C: SIGINT to the foreground pgrp */
			cnputc('^');
			cnputc('C');
			cnputc('\n');
			sig_send_pgrp(console_fg_pgrp, SIGINT);
			return (-EINTR);
		}
		if (c == 0x1a) {		/* ^Z: SIGTSTP */
			cnputc('^');
			cnputc('Z');
			cnputc('\n');
			sig_send_pgrp(console_fg_pgrp, SIGTSTP);
			return (-EINTR);
		}
		if (c == 0x1c) {		/* ^\: SIGQUIT */
			cnputc('^');
			cnputc('\\');
			cnputc('\n');
			sig_send_pgrp(console_fg_pgrp, SIGQUIT);
			return (-EINTR);
		}
		if (c < 0x20 && c != '\t')
			continue;
		cnputc(c);
		buf[i++] = c;
	}
	return ((int)i);
}

/*
 * Termios for the console.  There is no real UART line discipline: the
 * console is always in canonical mode with echo, which is exactly what
 * isatty(3)/sh want to see.
 */
/* 中文：控制台 termios 处理：get 时填充规范模式+回显的 termios 结构，set 时接受但忽略（标志固定） */
/* EN: Console termios handling: on get fill a canonical+echo termios struct; on set accept but ignore since flags are fixed */
static int
console_termios(unsigned arg, int get)
{
	struct termios *t = (struct termios *)arg;

	if (!user_ok(arg, sizeof(*t)))
		return (-EFAULT);
	if (get) {
		bzero(t, sizeof(*t));
		t->c_iflag = ICRNL | IXON;
		t->c_oflag = OPOST | ONLCR;
		t->c_cflag = CS8 | CREAD | CLOCAL | B9600;
		t->c_lflag = ICANON | ECHO | ECHOE | ISIG | IEXTEN;
		t->c_cc[VINTR] = 3;
		t->c_cc[VEOF] = 4;
		t->c_cc[VERASE] = 0x7f;
		t->c_cc[VKILL] = 0x15;
		t->c_cc[VSTART] = 0x11;
		t->c_cc[VSTOP] = 0x13;
		t->c_cc[VSUSP] = 0x1a;
		t->c_cc[VMIN] = 1;
		t->c_cc[VTIME] = 0;
	}
	/* TIOCSETA*: accept and ignore, the console flags are fixed */
	return (0);
}

/*
 * The native 4.5BSD service ABI.  Personalities call this after translating
 * their own call numbers; it is the only function that knows the syscall
 * switch internals.
 */
/*
 * S25: user threads.  The main thread's context remains in uproc.regs (so
 * the existing fork/wait/pipe paths are unchanged); additional threads keep
 * their context in kthread.t_regs and are scheduled by the thread table.
 */
/* 中文：创建用户级线程：分配 tid 与用户栈（或采用指定栈顶）、生成 kthread、设置入口/参数/上下文并置为可运行，返回 tid */
/* EN: Create a user thread: allocate a tid and user stack (or use a given top), spawn a kthread, set entry/arg/context and mark runnable, returning the tid */
static int
thread_create(struct sysregs *r)
{
	struct kthread *t;
	int tid;
	unsigned sva, top;

	tid = cur->p_next_tid++;
	if (r->edx != 0)
		top = r->edx;
	else {
		/* per-thread user stack: 2 pages below the main stack */
		sva = 0xBFA00000u + ((unsigned)(tid & 0xf) << 16);
		user_map(cur->pd, sva, page_alloc());
		user_map(cur->pd, sva + PAGE_SIZE, page_alloc());
		top = sva + 2 * PAGE_SIZE - 16;
	}
	t = kthread_user_spawn(cur, tid);
	if (t == 0)
		return (-EAGAIN);
	t->t_regs = *r;
	t->t_regs.eip = r->ebx;		/* entry */
	t->t_regs.eax = r->ecx;		/* arg */
	t->t_regs.uesp = top;		/* user stack top */
	t->t_regs.ebp = 0;
	t->t_sigmask = (cur_thread != 0) ? cur_thread->t_sigmask :
	    cur->p_sigmask;
	t->t_state = KT_RUN;
	cur->p_nthreads++;
	return (tid);
}

/* 中文：终止当前线程：主线程走 proc_exit，其余线程置僵尸、减少计数、唤醒 join 者，分离线程则释放，最后调度 */
/* EN: Exit the current thread: main thread calls proc_exit; others become zombie, decrement count, wake joiners, free if detached, then schedule */
static void
thread_exit_current(int code)
{
	struct kthread *t = cur_thread;
	int i;

	if (t == 0 || t->t_tid == cur->pid) {
		if (cur->p_nthreads <= 1) {
			proc_exit(code);
			return;
		}
		/* main thread exits, other threads keep the process alive */
		t->t_exitcode = code;
		t->t_state = KT_ZOMBIE;
		cur->state = P_BLOCKED;
		proc_sched();
		return;
	}
	t->t_exitcode = code;
	t->t_state = KT_ZOMBIE;
	cur->p_nthreads--;
	for (i = 0; i < NKTHREAD; i++) {
		struct kthread *j = &kthreads[i];

		if (!j->t_used || j->t_join_tid != t->t_tid)
			continue;
		j->t_join_tid = 0;
		if (j->t_tid == j->t_proc->pid) {
			j->t_proc->regs.eax = code;
			j->t_proc->state = P_RUN;
		} else {
			j->t_regs.eax = code;
			j->t_state = KT_RUN;
		}
	}
	if (t->t_detached) {
		cur_thread = 0;
		kthread_user_free(t);
	}
	proc_sched();
}

/* 中文：分离线程 tid：找不到返回 ESRCH，已被 join 返回 EINVAL，否则置分离标志 */
/* EN: Detach thread tid: ESRCH if not found, EINVAL if already joined, else set the detached flag */
static int
thread_detach(int tid)
{
	struct kthread *t;

	t = kthread_user_find(tid);
	if (t == 0)
		return (-ESRCH);
	if (t->t_join_tid != 0)
		return (-EINVAL);
	t->t_detached = 1;
	return (0);
}

/* 中文：等待线程 tid：已僵尸则取退出码并释放，否则当前线程记录 join_tid、保存上下文、阻塞并调度 */
/* EN: Join thread tid: if zombie take its exit code and free it, else record join_tid, save context, block and schedule */
static int
thread_join(int tid, struct sysregs *r)
{
	struct kthread *t, *jt;
	int code;

	t = kthread_user_find(tid);
	if (t == 0)
		return (-ESRCH);
	if (t->t_state == KT_ZOMBIE) {
		code = t->t_exitcode;
		kthread_user_free(t);
		if (cur->p_nthreads > 1)
			cur->p_nthreads--;
		return (code);
	}
	jt = cur_thread;
	jt->t_join_tid = tid;
	if (jt->t_tid == cur->pid) {
		cur->regs = *r;
		cur->state = P_BLOCKED;
	} else {
		jt->t_regs = *r;
		jt->t_state = KT_BLOCK;
	}
	proc_sched();
	return (0);
}

/* 中文：返回当前线程的 tid，无当前线程时返回进程 pid */
/* EN: Return the current thread's tid, or the process pid when there is no current thread */
static int
thread_self(void)
{

	return (cur_thread != 0 ? cur_thread->t_tid : cur->pid);
}

/* 中文：线程让出 CPU：保存当前上下文并置为可运行，然后调度其它线程 */
/* EN: Yield the CPU: save the current context and mark it runnable, then schedule another thread */
static void
thread_yield(struct sysregs *r)
{

	if (cur_thread == 0)
		return;
	if (cur_thread->t_tid == cur->pid) {
		cur->regs = *r;
		cur->state = P_RUN;
	} else {
		cur_thread->t_regs = *r;
		cur_thread->t_state = KT_RUN;
	}
	proc_sched();
}

/* 中文：原生 4.5BSD 系统调用分发器：按 eax 调用号分派到各系统调用实现，结果写回 r->eax */
/* EN: Native 4.5BSD system-call dispatcher: dispatch on the eax call number to the various syscall implementations, writing the result back to r->eax */
int
bsd45_syscall(struct sysregs *r)
{
	unsigned n = r->eax;

	switch (n) {
	case 430:			/* getticks(): PIT ticks since boot */
		r->eax = clock_get_ticks();
		break;
	case 460:			/* aio_submit(fd, op, buf, len, off) */
		r->eax = (unsigned)aio_submit_sys((int)r->ebx, (int)r->ecx,
		    r->edx, r->esi, r->edi);
		break;
	case 461:			/* aio_wait(token) */
		r->eax = (unsigned)aio_wait_sys((int)r->ebx, r);
		break;
	case 462:			/* aio_poll(token) */
		r->eax = (unsigned)aio_poll_sys((int)r->ebx);
		break;
	case 463:			/* uring_setup(entries) -> user VA */
		r->eax = (unsigned)uring_setup_sys(r->ebx);
		break;
	case 464:			/* uring_enter(to_submit, wait) */
		r->eax = (unsigned)uring_enter_sys(r->ebx, (int)r->ecx, r);
		break;
	case 465:			/* uring_teardown() */
		r->eax = (unsigned)uring_teardown_sys();
		break;
	case 431: {			/* getcow(int *v): shared/faults/copies */
		unsigned v[3];
		int rv;

		v[0] = cow_shared;
		v[1] = cow_faults;
		v[2] = cow_copies;
		rv = ucopyout(v, (void *)r->ebx, sizeof(v));
		r->eax = (rv < 0) ? (unsigned)rv : 0;
		break;
	}
	case 4: {			/* write(fd, buf, len) */
		unsigned fd = r->ebx, buf = r->ecx, len = r->edx;
		unsigned i;

		if (!user_ok(buf, len)) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		if (fd >= NFD) {
			r->eax = (unsigned)-EBADF;
			break;
		}
		if (cur->fds[fd].used && cur->fds[fd].vp != 0) {
			int n2;

			/*
			 * Synchronous VFS write: vfs_write_sync() runs the
			 * backend in this context; async uses vfs_write_async.
			 * 同步 VFS 写：vfs_write_sync() 在本地上下文执行后端；
			 * 异步走 vfs_write_async。
			 */
			n2 = vfs_write_sync(cur->fds[fd].vp,
			    cur->fds[fd].off, (const void *)buf, len);
			if (n2 > 0)
				cur->fds[fd].off += n2;
			r->eax = (unsigned)n2;
			break;
		}
		if (cur->fds[fd].used && cur->fds[fd].type == FD_TMPFILE) {
			struct tmpfs_node *n = &tmpfs[cur->fds[fd].node];
			unsigned off = cur->fds[fd].off, n2 = len;

			if (off >= TMPFS_DATASZ) {
				r->eax = (unsigned)-ENOSPC;
				break;
			}
			if (n2 > TMPFS_DATASZ - off)
				n2 = TMPFS_DATASZ - off;
			bcopy((char *)buf, n->data + off, n2);
			cur->fds[fd].off = off + n2;
			if (cur->fds[fd].off > n->size)
				n->size = cur->fds[fd].off;
			cur->fds[fd].len = n->size;
			r->eax = n2;
			break;
		}
		if (cur->fds[fd].used && cur->fds[fd].type == FD_PIPE) {
			struct pipe *pp = &pipes[cur->fds[fd].pipeidx];
			unsigned space, n, first;

			if (cur->fds[fd].rw != 2) {
				r->eax = (unsigned)-EBADF;
				break;
			}
			if (pp->readers <= 0) {
				r->eax = (unsigned)-EPIPE;
				break;
			}
			space = PIPESZ - pp->count;
			if (space == 0) {
				cur->regs = *r;
				cur->state = P_BLOCKED;
				cur->pipeidx = pp - pipes;
				cur->piperw = 2;
				cur->pipeat = buf;
				cur->pipelen = len;
				pp->wpid = cur->pid;
				proc_sched();
				break;		/* resumed by the reader */
			}
			n = len;
			if (n > space)
				n = space;
			first = PIPESZ - pp->wpos;
			if (first > n)
				first = n;
			bcopy((char *)buf, pp->buf + pp->wpos, first);
			if (n > first)
				bcopy((char *)buf + first, pp->buf, n - first);
			pp->wpos = (pp->wpos + n) % PIPESZ;
			pp->count += n;
			pipe_wake_reader(pp, 0);
			r->eax = n;
			break;
		}
		if (cur->fds[fd].used &&
		    (cur->fds[fd].type == FD_NULL ||
		     cur->fds[fd].type == FD_ZERO)) {
			r->eax = len;		/* discard */
			break;
		}
		if (!cur->fds[fd].used ||
		    cur->fds[fd].type != FD_CONSOLE) {	/* not writable */
			r->eax = (unsigned)-EBADF;
			break;
		}
		for (i = 0; i < len; i++)
			cnputc(((const char *)buf)[i]);
		r->eax = len;
		break;
	}
	case 3: {			/* read(fd, buf, len) */
		unsigned fd = r->ebx, buf = r->ecx, len = r->edx;
		unsigned i = 0;

		if (!user_ok_w(buf, len)) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		if (len == 0) {
			r->eax = 0;
			break;
		}
		if (fd >= NFD || !cur->fds[fd].used) {
			r->eax = (unsigned)-EBADF;
			break;
		}
		if (cur->fds[fd].type == FD_NULL) {
			r->eax = 0;
			break;
		}
		if (cur->fds[fd].type == FD_ZERO) {
			for (i = 0; i < len; i++)
				((char *)buf)[i] = 0;
			r->eax = len;
			break;
		}
		if (cur->fds[fd].type == FD_PS) {
			static char pbuf[512];
			static int plen = -1;
			unsigned off = cur->fds[fd].off, n2;

			if (plen < 0)
				plen = proc_list_text(pbuf, sizeof(pbuf));
			if (off >= (unsigned)plen) {
				r->eax = 0;
				break;
			}
			n2 = plen - off;
			if (n2 > len)
				n2 = len;
			bcopy(pbuf + off, (char *)buf, n2);
			cur->fds[fd].off = off + n2;
			r->eax = n2;
			break;
		}
		if (cur->fds[fd].type == FD_MOUNTS) {
			static char mbuf[256];
			static int mlen = -1;
			unsigned off = cur->fds[fd].off, n2;

			if (mlen < 0)
				mlen = vfs_mount_list(mbuf, sizeof(mbuf));
			if (off >= (unsigned)mlen) {
				r->eax = 0;
				break;
			}
			n2 = mlen - off;
			if (n2 > len)
				n2 = len;
			bcopy(mbuf + off, (char *)buf, n2);
			cur->fds[fd].off = off + n2;
			r->eax = n2;
			break;
		}
		if (cur->fds[fd].type == FD_VMSTAT) {
			/*
			 * S17: the counters are live, so re-render on every
			 * read instead of caching a snapshot like /dev/ps.
			 */
			static char vbuf[512];
			int vlen = vfs_cache_report(vbuf, sizeof(vbuf));
			unsigned off = cur->fds[fd].off, n2;

			if (off >= (unsigned)vlen) {
				r->eax = 0;
				break;
			}
			n2 = vlen - off;
			if (n2 > len)
				n2 = len;
			bcopy(vbuf + off, (char *)buf, n2);
			cur->fds[fd].off = off + n2;
			r->eax = n2;
			break;
		}
		if (cur->fds[fd].type == FD_CONSOLE) {
			r->eax = (unsigned)console_read((char *)buf, len);
			break;
		}
		if (cur->fds[fd].type == FD_DIR) {
			r->eax = (unsigned)-EISDIR;
			break;
		}
		if (cur->fds[fd].vp != 0) {
			int n2;

			if (cur->fds[fd].vp->type == VDIR) {
				r->eax = (unsigned)-EISDIR;
				break;
			}
			n2 = vfs_read_sync(cur->fds[fd].vp,
			    cur->fds[fd].off, (void *)buf, len);
			if (n2 > 0)
				cur->fds[fd].off += n2;
			r->eax = (unsigned)n2;
			break;
		}
		if (cur->fds[fd].type == FD_TMPFILE) {
			struct tmpfs_node *n = &tmpfs[cur->fds[fd].node];
			unsigned off = cur->fds[fd].off, n2 = 0;

			if (cur->fds[fd].rw != 1 && cur->fds[fd].rw != 2) {
				r->eax = (unsigned)-EBADF;
				break;
			}
			if (off < n->size) {
				n2 = n->size - off;
				if (n2 > len)
					n2 = len;
				bcopy(n->data + off, (char *)buf, n2);
				cur->fds[fd].off = off + n2;
			}
			r->eax = n2;
			break;
		}
		if (cur->fds[fd].type == FD_TMPDIR) {
			r->eax = (unsigned)-EISDIR;
			break;
		}
		if (cur->fds[fd].type == FD_PIPE) {
			struct pipe *pp = &pipes[cur->fds[fd].pipeidx];
			unsigned n, first;

			if (cur->fds[fd].rw != 1) {
				r->eax = (unsigned)-EBADF;
				break;
			}
			if (pp->count == 0) {
				if (pp->writers <= 0) {		/* EOF */
					r->eax = 0;
					break;
				}
				cur->regs = *r;
				cur->state = P_BLOCKED;
				cur->pipeidx = pp - pipes;
				cur->piperw = 1;
				cur->pipeat = buf;
				cur->pipelen = len;
				pp->rpid = cur->pid;
				proc_sched();
				break;		/* resumed by the writer */
			}
			n = pp->count;
			if (n > len)
				n = len;
			first = PIPESZ - pp->rpos;
			if (first > n)
				first = n;
			bcopy(pp->buf + pp->rpos, (char *)buf, first);
			if (n > first)
				bcopy(pp->buf, (char *)buf + first, n - first);
			pp->rpos = (pp->rpos + n) % PIPESZ;
			pp->count -= n;
			pipe_wake_writer(pp, 0);
			r->eax = n;
			break;
		}
		while (i < len && cur->fds[fd].off < cur->fds[fd].len)
			((char *)buf)[i++] = cur->fds[fd].d[cur->fds[fd].off++];
		r->eax = i;
		break;
	}
	case 5: {			/* open(path, flags, mode) */
		char path[128];
		struct vnode *vp = 0;
		int fd, flags = (int)r->ecx, rw, rv;
		unsigned mode = 0, size = 0;

		if (path_arg(r->ebx, path, sizeof(path)) < 0) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		/* S11: the three character devices live outside the VFS */
		if (strcmp(path, "/dev/null") == 0 ||
		    strcmp(path, "/dev/zero") == 0 ||
		    strcmp(path, "/dev/tty") == 0 ||
		    strcmp(path, "/dev/mounts") == 0 ||
		    strcmp(path, "/dev/ps") == 0 ||
		    strcmp(path, "/dev/vmstat") == 0) {
			int t;

			t = (strcmp(path, "/dev/null") == 0) ? FD_NULL :
			    (strcmp(path, "/dev/zero") == 0) ? FD_ZERO :
			    (strcmp(path, "/dev/mounts") == 0) ? FD_MOUNTS :
			    (strcmp(path, "/dev/ps") == 0) ? FD_PS :
			    (strcmp(path, "/dev/vmstat") == 0) ? FD_VMSTAT :
			    FD_CONSOLE;
			for (fd = 0; fd < NFD; fd++)
				if (!cur->fds[fd].used)
					break;
			if (fd >= NFD) {
				r->eax = (unsigned)-EMFILE;
				break;
			}
			bzero(&cur->fds[fd], sizeof(cur->fds[fd]));
			cur->fds[fd].used = 1;
			cur->fds[fd].type = t;
			cur->fds[fd].rw = ((flags & O_ACCMODE) == O_RDONLY) ? 1 : 2;
			cur->fds[fd].pipeidx = -1;
			cur->fds[fd].node = -1;
			cur->fds[fd].vp = 0;
			r->eax = (unsigned)fd;
			break;
		}
		rv = vfs_namei(path, &vp);
		if (rv < 0) {
			if (!(flags & O_CREAT)) {
				r->eax = (unsigned)-ENOENT;
				break;
			}
			rv = vfs_create(path, 0, &vp);
			if (rv < 0) {
				r->eax = (unsigned)rv;
				break;
			}
		}
		rw = ((flags & O_ACCMODE) == O_RDONLY) ? 1 : 2;
		if (flags & O_TRUNC) {
			if (vp->ops->truncate == 0) {
				vn_put(vp);
				r->eax = (unsigned)-EROFS;
				break;
			}
			vp->ops->truncate(vp);
		}
		if (flags & O_APPEND) {
			vp->ops->getattr(vp, &mode, &size);
		}
		fd = fd_alloc_vnode(vp, rw, (flags & O_APPEND) ? size : 0);
		if (fd < 0) {
			vn_put(vp);
			r->eax = (unsigned)-EMFILE;
			break;
		}
		r->eax = (unsigned)fd;
		break;
	}
	case 6:				/* close(fd) */
		fd_close((int)r->ebx);
		r->eax = 0;
		break;
	case 59: {			/* execve(path, argv, envp) */
		char path[128];

		if (path_arg(r->ebx, path, sizeof(path)) < 0) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		proc_setcomm(cur, path);
		n = (unsigned)exec_image(path, r->ecx, r->edx, r);
		if ((int)n < 0)
			r->eax = n;
		break;
	}
	case 1:				/* exit(code) */
		proc_exit((int)r->ebx);
		break;
	case 17: {			/* break(addr) */
		unsigned a = r->ebx;

		if (a < USER_LOAD || a > USER_HEAP_MAX) {
			r->eax = (unsigned)-EINVAL;
			break;
		}
		/*
		 * Demand-paged heap: only record the new limit.  The first
		 * touch below brk_mapped takes a page fault and gets a zero
		 * page from demand_page(); no page is allocated here.
		 * 按需分页堆：这里只记录新的堆上限；首次访问 brk_mapped
		 * 以下的地址时由 demand_page() 分配零页，本调用不分配物理页。
		 */
		if (a > cur->brk_mapped)
			cur->brk_mapped = a;
		r->eax = a;
		break;
	}
	case 20:			/* getpid */
		r->eax = cur->pid;
		break;
	case 39:			/* getppid */
		r->eax = cur->ppid;
		break;
	case 24: case 25: case 43: case 47:	/* uid/gid */
		r->eax = 0;
		break;
	case 81:			/* getpgrp */
		r->eax = (unsigned)cur->p_pgrp;
		break;
	case 82: {			/* setpgid(pid, pgid) */
		struct uproc *p = (r->ebx == 0) ? cur :
		    proc_find((int)r->ebx);

		if (p == 0) {
			r->eax = (unsigned)-ESRCH;
			break;
		}
		p->p_pgrp = (r->ecx == 0) ? p->pid : (int)r->ecx;
		r->eax = 0;
		break;
	}
	case 33: {			/* access(path, mode) */
		char path[128];
		struct vnode *vp = 0;

		if (path_arg(r->ebx, path, sizeof(path)) < 0) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		r->eax = (vfs_namei(path, &vp) == 0) ? 0 : (unsigned)-ENOENT;
		if (vp)
			vn_put(vp);
		break;
	}
	case 37: {			/* kill(pid, sig) */
		int pid = (int)r->ebx, sig = (int)r->ecx, i, found = 0;

		if (sig < 0 || sig >= NSIG) {
			r->eax = (unsigned)-EINVAL;
			break;
		}
		for (i = 0; i < NPROC; i++) {
			struct uproc *p = &procs[i];
			int hit;

			if (p->state == P_FREE)
				continue;
			if (pid > 0)
				hit = (p->pid == pid);
			else if (pid == 0)
				hit = (p->p_pgrp == cur->p_pgrp);
			else if (pid == -1)
				hit = (p->pid != 1);
			else
				hit = (p->p_pgrp == -pid);
			if (hit) {
				sig_send(p, sig);
				found = 1;
			}
		}
		r->eax = found ? 0 : (unsigned)-ESRCH;
		break;
	}
	case 46: {			/* sigaction(sig, nsa, osa) */
		struct {
			unsigned handler, mask, flags;
		} sa, old;
		int sig = (int)r->ebx;
		unsigned nsa = r->ecx, osa = r->edx;

		if (sig <= 0 || sig >= NSIG) {
			r->eax = (unsigned)-EINVAL;
			break;
		}
		if (osa != 0) {
			if (!user_ok(osa, sizeof(sa))) {
				r->eax = (unsigned)-EFAULT;
				break;
			}
			old.handler = cur->p_sighand[sig];
			old.mask = cur->p_sigmask;
			old.flags = 0;
			bcopy(&old, (void *)osa, sizeof(sa));
		}
		if (nsa == 0) {
			r->eax = 0;
			break;
		}
		if (!user_ok(nsa, sizeof(sa))) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		bcopy((void *)nsa, &sa, sizeof(sa));
		cur->p_sighand[sig] = sa.handler;
		cur->p_sigcatch &= ~SIG_TO_MASK(sig);
		cur->p_sigignore &= ~SIG_TO_MASK(sig);
		if (sa.handler == SIG_IGN)
			cur->p_sigignore |= SIG_TO_MASK(sig);
		else if (sa.handler != SIG_DFL)
			cur->p_sigcatch |= SIG_TO_MASK(sig);
		r->eax = 0;
		break;
	}
	case 48: {			/* sigprocmask(how, set, oset) */
		unsigned how = r->ebx, set = r->ecx, oset = r->edx;
		unsigned m = 0, old;
		unsigned *maskp = &cur->p_sigmask;

		if (cur_thread != 0 && cur_thread->t_proc == cur)
			maskp = &cur_thread->t_sigmask;
		old = *maskp;
		if (set != 0) {
			if (!user_ok(set, 4)) {
				r->eax = (unsigned)-EFAULT;
				break;
			}
			m = *(unsigned *)set;
		}
		if (oset != 0) {
			if (!user_ok(oset, 4)) {
				r->eax = (unsigned)-EFAULT;
				break;
			}
			*(unsigned *)oset = old;
		}
		if (set != 0) {
			if (how == 1)
				*maskp |= m;
			else if (how == 2)
				*maskp &= ~m;
			else if (how == 3)
				*maskp = m &
				    ~(SIG_TO_MASK(SIGKILL) |
				      SIG_TO_MASK(SIGSTOP));
			else {
				r->eax = (unsigned)-EINVAL;
				break;
			}
			if (cur_thread == cur->p_thread)
				cur->p_sigmask = cur_thread->t_sigmask;
		}
		r->eax = 0;
		break;
	}
	case 103: {			/* sigreturn: restore the saved context */
		struct sigframe *fp = 0;
		unsigned a, i;

		a = (r->uesp + 4 + SIGTRAMP) & ~3u;
		if (user_ok(a, SIGFRAME_SIZE) &&
		    ((struct sigframe *)a)->magic == SIGFRAME_MAGIC)
			fp = (struct sigframe *)a;
		else
			for (i = r->uesp & ~3u; i < r->uesp + 320; i += 4)
				if (user_ok(i, SIGFRAME_SIZE) &&
				    ((struct sigframe *)i)->magic ==
				    SIGFRAME_MAGIC) {
					fp = (struct sigframe *)i;
					break;
				}
		if (fp == 0) {
			r->eax = (unsigned)-EINVAL;
			break;
		}
		r->eax = fp->eax; r->ebx = fp->ebx; r->ecx = fp->ecx;
		r->edx = fp->edx; r->esi = fp->esi; r->edi = fp->edi;
		r->ebp = fp->ebp; r->ds = fp->ds; r->es = fp->es;
		r->eip = fp->eip; r->cs = fp->cs; r->eflags = fp->eflags;
		r->uesp = fp->uesp; r->ss = fp->ss;
		cur->p_sigmask = fp->mask;
		break;
	}
	case 52:			/* sigpending */
		r->eax = 0;
		break;
	case 54: {			/* ioctl(fd, request, arg) */
		unsigned fd = r->ebx;
		unsigned long req = r->ecx;
		unsigned arg = r->edx;

		if (fd > 2) {
			r->eax = (unsigned)-ENOTTY;
			break;
		}
		switch (req) {
		case TIOCGETA:
			r->eax = (unsigned)console_termios(arg, 1);
			break;
		case TIOCSETA:
		case TIOCSETAW:
		case TIOCSETAF:
			r->eax = (unsigned)console_termios(arg, 0);
			break;
		case TIOCGWINSZ: {
			struct winsize *w = (struct winsize *)arg;

			if (!user_ok(arg, sizeof(*w))) {
				r->eax = (unsigned)-EFAULT;
				break;
			}
			w->ws_row = 25;
			w->ws_col = 80;
			w->ws_xpixel = 0;
			w->ws_ypixel = 0;
			r->eax = 0;
			break;
		}
		case TIOCGPGRP:
			if (!user_ok(arg, 4)) {
				r->eax = (unsigned)-EFAULT;
				break;
			}
			*(int *)arg = console_fg_pgrp;
			r->eax = 0;
			break;
		case TIOCSPGRP:
			if (!user_ok(arg, 4)) {
				r->eax = (unsigned)-EFAULT;
				break;
			}
			console_fg_pgrp = *(int *)arg;
			r->eax = 0;
			break;
		default:
			r->eax = (unsigned)-ENOTTY;
			break;
		}
		break;
	}
	case 60: {			/* umask(mask) */
		unsigned old = umask_val;

		umask_val = r->ebx;
		r->eax = old;
		break;
	}
	case 189: {			/* stat(path, sb) */
		char path[128];
		struct vnode *vp = 0;
		unsigned mode = 0, size = 0;

		if (path_arg(r->ebx, path, sizeof(path)) < 0 ||
		    !user_ok(r->ecx, STAT_SIZE)) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		if (vfs_namei(path, &vp) < 0) {
			r->eax = (unsigned)-ENOENT;
			break;
		}
		vp->ops->getattr(vp, &mode, &size);
		fill_stat(mode, size, (unsigned *)r->ecx);
		vn_put(vp);
		r->eax = 0;
		break;
	}
	case 188: {			/* fstat(fd, sb) */
		if (!user_ok(r->ecx, STAT_SIZE)) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		if (r->ebx < 3) {
			fill_stat(0020620, 0, (unsigned *)r->ecx);
			r->eax = 0;
			break;
		}
		if (r->ebx >= NFD || !cur->fds[r->ebx].used) {
			r->eax = (unsigned)-EBADF;
			break;
		}
		if (cur->fds[r->ebx].vp != 0) {
			unsigned mode = 0, size = 0;

			cur->fds[r->ebx].vp->ops->getattr(
			    cur->fds[r->ebx].vp, &mode, &size);
			fill_stat(mode, size, (unsigned *)r->ecx);
			r->eax = 0;
			break;
		}
		if (cur->fds[r->ebx].type == FD_TMPFILE) {
			fill_stat(0100644,
			    tmpfs[cur->fds[r->ebx].node].size,
			    (unsigned *)r->ecx);
			r->eax = 0;
			break;
		}
		if (cur->fds[r->ebx].type == FD_TMPDIR) {
			fill_stat(0040755, 4096, (unsigned *)r->ecx);
			r->eax = 0;
			break;
		}
		fill_stat(cur->fds[r->ebx].type == FD_DIR ? 0040755 : 0100755,
		    cur->fds[r->ebx].len, (unsigned *)r->ecx);
		r->eax = 0;
		break;
	}
	case 196: {			/* getdirentries(fd, buf, n, basep) */
		unsigned fd = r->ebx, buf = r->ecx, nb = r->edx, basep = r->esi;
		struct fd *fp;
		char name[48];
		unsigned elba, esize, pos;
		int eflags, rv, namlen, reclen;

		if (fd >= NFD || !cur->fds[fd].used ||
		    (cur->fds[fd].type != FD_DIR &&
		     cur->fds[fd].type != FD_TMPDIR)) {
			r->eax = (unsigned)-EBADF;
			break;
		}
		if (!user_ok(buf, nb) || nb < 16) {
			r->eax = (unsigned)-EINVAL;
			break;
		}
		fp = &cur->fds[fd];
		if (fp->vp != 0) {
			int rv2, namlen2, reclen2, isd2 = 0;

			rv2 = fp->vp->ops->readdir(fp->vp, &fp->off, name,
			    &isd2);
			if (rv2 < 0) {
				r->eax = (unsigned)-EIO;
				break;
			}
			if (basep != 0 && user_ok(basep, 4))
				*(unsigned *)basep = 0;
			if (rv2 > 0) {
				r->eax = 0;
				break;
			}
			namlen2 = strlen(name);
			reclen2 = (8 + namlen2 + 1 + 3) & ~3;
			if ((unsigned)reclen2 > nb) {
				r->eax = (unsigned)-EINVAL;
				break;
			}
			bzero((char *)buf, reclen2);
			*(unsigned *)(buf + 0) = fp->off;
			*(unsigned short *)(buf + 4) = reclen2;
			((char *)buf)[6] = isd2 ? 4 : 8;
			((char *)buf)[7] = namlen2;
			bcopy(name, (char *)buf + 8, namlen2 + 1);
			r->eax = reclen2;
			break;
		}
		if (fp->type == FD_TMPDIR) {
			int i;

			for (i = fp->dpos; i < NTMPFS && !tmpfs[i].used; i++)
				;
			if (basep != 0 && user_ok(basep, 4))
				*(unsigned *)basep = 0;
			if (i >= NTMPFS) {
				fp->dpos = NTMPFS;
				r->eax = 0;
				break;
			}
			fp->dpos = i + 1;
			namlen = strlen(tmpfs[i].name);
			reclen = (8 + namlen + 1 + 3) & ~3;
			if ((unsigned)reclen > nb) {
				r->eax = (unsigned)-EINVAL;
				break;
			}
			bzero((char *)buf, reclen);
			*(unsigned *)(buf + 0) = i + 1;
			*(unsigned short *)(buf + 4) = reclen;
			((char *)buf)[6] = tmpfs[i].isdir ? 4 : 8;
			((char *)buf)[7] = namlen;
			bcopy(tmpfs[i].name, (char *)buf + 8, namlen + 1);
			r->eax = reclen;
			break;
		}
		pos = fp->dpos;
		rv = iso_readdir(fp->lba, fp->dsize, &pos, name, &elba,
		    &esize, &eflags);
		if (rv < 0) {
			r->eax = (unsigned)-EIO;
			break;
		}
		if (basep != 0 && user_ok(basep, 4))
			*(unsigned *)basep = 0;
		if (rv > 0) {		/* end of directory */
			fp->dpos = pos;
			r->eax = 0;
			break;
		}
		namlen = strlen(name);
		reclen = (8 + namlen + 1 + 3) & ~3;
		if ((unsigned)reclen > nb) {
			r->eax = (unsigned)-EINVAL;
			break;
		}
		bzero((char *)buf, reclen);
		*(unsigned *)(buf + 0) = elba;		/* d_fileno */
		*(unsigned short *)(buf + 4) = reclen;	/* d_reclen */
		((char *)buf)[6] = (eflags & 2) ? 4 : 8;	/* d_type */
		((char *)buf)[7] = namlen;		/* d_namlen */
		bcopy(name, (char *)buf + 8, namlen + 1);
		fp->dpos = pos;
		r->eax = reclen;
		break;
	}
	case 199: {			/* lseek(fd, off, whence) */
		unsigned fd = r->ebx, off = r->ecx, wh = r->edx;

		if (fd >= NFD || !cur->fds[fd].used) {
			r->eax = (unsigned)-EBADF;
			break;
		}
		if (cur->fds[fd].vp != 0) {
			unsigned mode = 0, size = 0;

			cur->fds[fd].vp->ops->getattr(cur->fds[fd].vp,
			    &mode, &size);
			if (wh == 0)
				cur->fds[fd].off = off;
			else if (wh == 1)
				cur->fds[fd].off += off;
			else
				cur->fds[fd].off = size;
			r->eax = cur->fds[fd].off;
			break;
		}
		if (cur->fds[fd].type == FD_TMPFILE) {
			if (wh == 0)
				cur->fds[fd].off = off;
			else if (wh == 1)
				cur->fds[fd].off += off;
			else
				cur->fds[fd].off = tmpfs[cur->fds[fd].node].size;
			r->eax = cur->fds[fd].off;
			break;
		}
		if (cur->fds[fd].type == FD_TMPDIR) {
			cur->fds[fd].dpos = (wh == 1) ? cur->fds[fd].dpos + off
			    : 0;
			r->eax = cur->fds[fd].dpos;
			break;
		}
		if (cur->fds[fd].type == FD_DIR) {
			if (wh == 0)
				cur->fds[fd].dpos = off;
			else if (wh == 1)
				cur->fds[fd].dpos += off;
			else
				cur->fds[fd].dpos = cur->fds[fd].dsize;
			r->eax = cur->fds[fd].dpos;
			break;
		}
		if (wh == 0)
			cur->fds[fd].off = off;
		else if (wh == 1)
			cur->fds[fd].off += off;
		else
			cur->fds[fd].off = cur->fds[fd].len;
		r->eax = cur->fds[fd].off;
		break;
	}
	case 89:			/* getdtablesize */
		r->eax = NFD;
		break;
	case 197: {			/* mmap(addr, len, prot, flags, fd) */
		int rv = do_mmap(r->ebx, r->ecx, (int)r->edx, (int)r->esi,
		    (int)r->edi);

		r->eax = (unsigned)rv;
		break;
	}
	case 73:			/* munmap(addr, len) */
		r->eax = (unsigned)do_munmap(r->ebx, r->ecx);
		break;
	case 74:			/* mprotect(addr, len, prot) */
		r->eax = 0;		/* pages are mapped eagerly with prot */
		break;
	case 65:			/* msync(addr, len) */
		r->eax = (unsigned)do_msync(r->ebx, r->ecx);
		break;
	case 7:				/* wait4(pid, status, options, rusage) */
		do_wait4((int)r->ebx, r->ecx, (int)r->edx, r);
		break;
	case 2:				/* fork */
		r->eax = (unsigned)do_fork(r);
		break;
	case 66:			/* vfork */
		r->eax = (unsigned)do_vfork(r);
		break;
	case 92: {			/* fcntl(fd, cmd, arg) */
		int fd = (int)r->ebx, cmd = (int)r->ecx, arg = (int)r->edx;

		if (fd < 0 || fd >= NFD || !cur->fds[fd].used) {
			r->eax = (unsigned)-EBADF;
			break;
		}
		if (cmd == 0) {		/* F_DUPFD: lowest free fd >= arg */
			int newfd;

			for (newfd = arg; newfd < NFD; newfd++)
				if (!cur->fds[newfd].used)
					break;
			if (newfd >= NFD) {
				r->eax = (unsigned)-EMFILE;
				break;
			}
			cur->fds[newfd] = cur->fds[fd];
			fd_ref(&cur->fds[newfd]);
			if (cur->fds[newfd].type == FD_PIPE &&
			    cur->fds[newfd].pipeidx >= 0) {
				if (cur->fds[newfd].rw == 1)
					pipes[cur->fds[newfd].pipeidx].readers++;
				else
					pipes[cur->fds[newfd].pipeidx].writers++;
			}
			r->eax = newfd;
			break;
		}
		if (cmd >= 1 && cmd <= 4) {	/* F_GETFD/F_SETFD/F_GETFL/F_SETFL */
			r->eax = 0;
			break;
		}
		r->eax = (unsigned)-EINVAL;
		break;
	}
	case 41: {			/* dup(fd) */
		int from = (int)r->ebx, to;

		if (from < 0 || from >= NFD || !cur->fds[from].used) {
			r->eax = (unsigned)-EBADF;
			break;
		}
		for (to = 3; to < NFD; to++)
			if (!cur->fds[to].used)
				break;
		if (to >= NFD) {
			r->eax = (unsigned)-EMFILE;
			break;
		}
		cur->fds[to] = cur->fds[from];
		fd_ref(&cur->fds[to]);
		if (cur->fds[to].type == FD_PIPE && cur->fds[to].pipeidx >= 0) {
			if (cur->fds[to].rw == 1)
				pipes[cur->fds[to].pipeidx].readers++;
			else
				pipes[cur->fds[to].pipeidx].writers++;
		}
		r->eax = to;
		break;
	}
	case 42: {			/* pipe(fds) */
		struct pipe *pp;
		unsigned fds[2];
		int i, nfd = 0;

		if (!user_ok(r->ebx, 2 * sizeof(int))) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		pp = pipe_alloc();
		if (pp == 0) {
			r->eax = (unsigned)-EMFILE;
			break;
		}
		pp->readers = 1;
		pp->writers = 1;
		for (i = 3; i < NFD && nfd < 2; i++) {
			if (!cur->fds[i].used) {
				fds[nfd++] = i;
				bzero(&cur->fds[i], sizeof(cur->fds[i]));
				cur->fds[i].used = 1;
				cur->fds[i].type = FD_PIPE;
				cur->fds[i].pipeidx = pp - pipes;
				cur->fds[i].d = 0;
				cur->fds[i].len = 0;
				cur->fds[i].off = 0;
				cur->fds[i].lba = 0;
				cur->fds[i].dsize = 0;
				cur->fds[i].dpos = 0;
				cur->fds[i].rw = (nfd == 1) ? 1 : 2;
			}
		}
		if (nfd < 2) {
			pp->used = 0;
			r->eax = (unsigned)-EMFILE;
			break;
		}
		((int *)r->ebx)[0] = fds[0];
		((int *)r->ebx)[1] = fds[1];
		r->eax = 0;
		break;
	}
	case 90: {			/* dup2(from, to) */
		int from = (int)r->ebx, to = (int)r->ecx;

		if (from < 0 || from >= NFD || to < 0 || to >= NFD ||
		    !cur->fds[from].used) {
			r->eax = (unsigned)-EBADF;
			break;
		}
		if (from == to) {
			r->eax = to;
			break;
		}
		fd_close(to);
		cur->fds[to] = cur->fds[from];
		fd_ref(&cur->fds[to]);
		if (cur->fds[to].type == FD_PIPE && cur->fds[to].pipeidx >= 0) {
			if (cur->fds[to].rw == 1)
				pipes[cur->fds[to].pipeidx].readers++;
			else
				pipes[cur->fds[to].pipeidx].writers++;
		}
		r->eax = to;
		break;
	}
	case 10: {			/* unlink(path) */
		char path[128];

		if (path_arg(r->ebx, path, sizeof(path)) < 0) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		r->eax = (unsigned)vfs_remove(path, 0);
		break;
	}
	case 128: {			/* rename(old, new) */
		char oldp[128], newp[128];

		if (path_arg(r->ebx, oldp, sizeof(oldp)) < 0 ||
		    path_arg(r->ecx, newp, sizeof(newp)) < 0) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		r->eax = (unsigned)vfs_rename(oldp, newp);
		break;
	}
	case 9:				/* link: no hard links yet */
		r->eax = (unsigned)-EROFS;
		break;
	case 15:			/* chmod: modes are fixed for now */
		r->eax = 0;
		break;
	case 136: {			/* mkdir(path, mode) */
		char path[128];
		struct vnode *vp = 0;
		int rv;

		if (path_arg(r->ebx, path, sizeof(path)) < 0) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		rv = vfs_create(path, 1, &vp);
		if (vp)
			vn_put(vp);
		r->eax = (unsigned)rv;
		break;
	}
	case 137: {			/* rmdir(path) */
		char path[128];

		if (path_arg(r->ebx, path, sizeof(path)) < 0) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		r->eax = (unsigned)vfs_remove(path, 1);
		break;
	}
	case 12: {			/* chdir(path) */
		char path[128];
		struct vnode *vp = 0;
		int rv;

		if (path_arg(r->ebx, path, sizeof(path)) < 0) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		rv = vfs_namei(path, &vp);
		if (rv < 0) {
			r->eax = (unsigned)rv;
			break;
		}
		if (vp->type != VDIR) {
			vn_put(vp);
			r->eax = (unsigned)-ENOTDIR;
			break;
		}
		vn_put(vp);
		bcopy(path, cur->p_cwd, sizeof(cur->p_cwd));
		r->eax = 0;
		break;
	}
	case 326: {			/* getcwd(buf, len) */
		unsigned buf = r->ebx, len = r->ecx;
		unsigned n = strlen(cur->p_cwd) + 1;

		if (len < n) {
			r->eax = (unsigned)-ERANGE;
			break;
		}
		if (!user_ok(buf, n)) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		bcopy(cur->p_cwd, (void *)buf, n);
		r->eax = buf;
		break;
	}
	case 116: {			/* gettimeofday(timeval *, timezone *) */
		unsigned tvp = r->ebx, tzp = r->ecx;
		struct timeval tv;

		if (tvp != 0 && !user_ok(tvp, sizeof(tv))) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		if (tzp != 0 && !user_ok(tzp, 8)) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		tv.tv_sec = rtc_epoch();
		tv.tv_usec = 0;
		if (tvp != 0)
			bcopy(&tv, (void *)tvp, sizeof(tv));
		if (tzp != 0)
			bzero((void *)tzp, 8);
		r->eax = 0;
		break;
	}
	case 164: {			/* uname(struct utsname *) */
		unsigned buf = r->ebx;
		struct utsname *up = (struct utsname *)buf;

		if (!user_ok(buf, sizeof(*up))) {
			r->eax = (unsigned)-EFAULT;
			break;
		}
		bzero(up, sizeof(*up));
		bcopy("BSD45", up->sysname, sizeof("BSD45"));
		bcopy("bsd45", up->nodename, sizeof("bsd45"));
		bcopy("1.0", up->release, sizeof("1.0"));
		bcopy("4.4BSD-Lite2 i386 ELF kernel", up->version,
		    sizeof("4.4BSD-Lite2 i386 ELF kernel"));
		bcopy("i386", up->machine, sizeof("i386"));
		r->eax = 0;
		break;
	}
	case 440:			/* thread_create(entry,arg,stack) */
		r->eax = (unsigned)thread_create(r);
		break;
	case 441:			/* thread_exit(code) */
		thread_exit_current((int)r->ebx);
		r->eax = 0;
		break;
	case 442:			/* thread_join(tid) */
		r->eax = (unsigned)thread_join((int)r->ebx, r);
		break;
	case 443:			/* thread_self() */
		r->eax = (unsigned)thread_self();
		break;
	case 444:			/* thread_yield() */
		thread_yield(r);
		r->eax = 0;
		break;
	case 445:			/* thread_detach(tid) */
		r->eax = (unsigned)thread_detach((int)r->ebx);
		break;
	case 451: {			/* tgkill(tid, sig) - thread-directed */
		int tid = (int)r->ebx, sig = (int)r->ecx;
		struct kthread *t;

		if (sig <= 0 || sig >= NSIG) {
			r->eax = (unsigned)-EINVAL;
			break;
		}
		t = kthread_user_find(tid);
		if (t == 0) {
			r->eax = (unsigned)-ESRCH;
			break;
		}
		t->t_sig |= SIG_TO_MASK(sig);
		if (t->t_state == KT_BLOCK)
			t->t_state = KT_RUN;
		t->t_proc->state = P_RUN;
		r->eax = 0;
		break;
	}
	case 450: {			/* access_w(const u16 *path) - W path */
#if KSTR_UTF16
		unsigned short ubuf[128], kbuf[128];
		struct kstr s;
		struct vnode *vp = 0;
		unsigned i, n = 0;

		for (i = 0; i < 128; i++) {
			if (!user_ok(r->ebx + i * 2, 2)) {
				r->eax = (unsigned)-EFAULT;
				break;
			}
			ubuf[i] = *(unsigned short *)(r->ebx + i * 2);
			if (ubuf[i] == 0)
				break;
			n++;
		}
		if (i >= 128 || n == 0) {
			r->eax = (unsigned)-EINVAL;
			break;
		}
		kstr_init(&s, kbuf, sizeof(kbuf));
		if (kstr_set_w(&s, ubuf, n * 2) < 0) {
			r->eax = (unsigned)-EINVAL;
			break;
		}
		i = (unsigned)vfs_namei_k(&s, &vp);
		if (vp != 0)
			vn_put(vp);
		r->eax = i;
#else
		r->eax = (unsigned)-ENOSYS;
#endif
		break;
	}
	}

	/* S10.2: deliver one pending signal on the way back to user mode */
	if (cur != 0)
		sig_deliver(cur, r);
	return (0);
}

/*
 * Trap entry point (called from usermode_asm.s).  The old personality
 * subsystem (subsys/) is gone: syscalls dispatch straight to bsd45_syscall().
 */
/* 中文：陷阱入口：personality 子系统已删除，系统调用直接分发到 bsd45_syscall */
/* EN: Trap entry: the personality subsystem is gone, dispatch straight to bsd45_syscall */
void
syscall_dispatch(struct sysregs *r)
{

	(void)bsd45_syscall(r);		/* writes the result into r->eax itself */
	/*
	 * Syscall exit is a safe point: drain deferred work before deciding
	 * whether to switch threads.
	 * 系统调用出口是安全点：先排空延迟工作，再决定是否切换线程。
	 */
	softirq_dispatch();
	/*
	 * Kernel-mode safe-point preemption: the syscall is complete, so the
	 * current user context can be saved and another thread scheduled.
	 * 内核态安全点抢占：系统调用已完成，可保存当前用户上下文并调度
	 * 其它线程；不在内核执行中途抢占。
	 */
	if (need_resched && cur != 0 && cur_thread != 0 &&
	    cur_thread->t_base_ipl == 0 && cur_thread->t_preemptable) {
		need_resched = 0;
		if (cur_thread->t_tid == cur->pid) {
			cur->regs = *r;
			cur->state = P_RUN;
		} else {
			cur_thread->t_regs = *r;
			cur_thread->t_state = KT_RUN;
		}
		proc_sched();
	}
}

/* 中文：安装系统调用门：委托 HAL 的 hal_syscall_gate_init 完成 IDT 设置 */
/* EN: Install the system-call gate: delegate to the HAL's hal_syscall_gate_init for IDT setup */
void
syscall_gate_init(void)
{

	hal_syscall_gate_init();
}

/* 中文：启动首个用户程序：经 VFS 读取并构建映像，初始化 pid 1 进程与主线程，安装系统调用门后经 enter_user 切到用户态 */
/* EN: Start the first user program: read and build the image via VFS, init pid 1 and its main thread, install the syscall gate and switch to user mode via enter_user */
void
start_prog(const char *path, const char *script)
{
	struct file *f;
	const unsigned char *img;
	unsigned len;
	unsigned pd, entry, esp;
	char *argv[4];
	int argc;
	char *envp[2];

	img = 0;
	len = 0;
	{
		struct vnode *vp = 0;
		unsigned char *buf;
		int rv;

		if (vfs_namei(path, &vp) < 0 || vp->type != VREG) {
			printf("no image for %s\n", path);
			return;
		}
		len = vp->size;
		buf = cd_map(len);
		if (buf == 0) {
			printf("no window for %s\n", path);
			vn_put(vp);
			return;
		}
		rv = vp->ops->read(vp, 0, buf, len);
		vn_put(vp);
		if (rv < 0 || (unsigned)rv != len) {
			printf("read failed for %s\n", path);
			return;
		}
		printf("exec %s: via vnode (%d bytes)\n", path, len);
		img = buf;
	}
	argv[0] = (char *)path;
	argc = 1;
	if (script != 0) {
		argv[argc++] = "-c";
		argv[argc++] = (char *)script;
	}
	envp[0] = "PATH=/bin:/sbin";

	pd = build_image(img, len, &entry, argv, argc, envp, 1, &esp);


	/* pid 1: the first user process, no parent */
	bzero(procs, sizeof(procs));
	cur = &procs[0];
	cur->state = P_RUN;
	cur->pid = 1;
	cur->ppid = 0;
	cur->pd = pd;
	cur->brk_mapped = USER_HEAP_START;
	cur->p_mmap_base = MMAP_BASE;
	cur->p_cwd[0] = '/';
	cur->p_cwd[1] = 0;
	proc_setcomm(cur, path);
	{
		int fd;

		for (fd = 0; fd < 3; fd++) {
			cur->fds[fd].used = 1;
			cur->fds[fd].type = FD_CONSOLE;
			cur->fds[fd].rw = (fd == 0) ? 1 : 2;
			cur->fds[fd].pipeidx = -1;
			cur->fds[fd].node = -1;
		}
	}
	next_pid = 2;
	(void)proc_add_thread(cur, 1);
	cur->p_next_tid = 101;
	if (cur->p_thread != 0)
		hal_kernel_stack_set(cur->p_thread->t_stack_top);

	syscall_gate_init();
	printf("exec %s: entry %x pd %x\n", path, entry, pd);
	enter_user(pd, entry, esp);
}

/* 中文：启动用户态入口：以 /sbin/init 和给定脚本调用 start_prog */
/* EN: Userland startup entry: call start_prog with /sbin/init and the given script */
void
start_userland(const char *script)
{

	start_prog("/sbin/init", script);
}


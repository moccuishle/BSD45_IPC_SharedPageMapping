/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * ipctest.c - S300 acceptance for kernel IPC and shared pages.
 * ipctest.c - S300 验收：内核 IPC 与共享页映射。
 *
 * 验证三项（KP-4/6/7）：
 *   1. KP-6 同进程消息往返：send 后 recv，内核填写 from；
 *   2. KP-6 跨进程会合：子进程注册端口并 recv，父进程 send，子进程收到；
 *   3. KP-7 共享页：分配物理页，映射到用户 VA，写入读回，解除并释放。
 * Verifies: (1) same-process message round trip, (2) cross-process rendezvous
 * via fork, (3) shared page map/write/read/unmap/free.
 *
 * 注：libexec 是用户态，内联汇编不受 asmble.md（仅约束 kernel/）限制。
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* syscall 编号 / syscall numbers */
#define SYS_PORT_REGISTER	452
#define SYS_PORT_RELEASE	453
#define SYS_SEND		454
#define SYS_RECV		455
#define EAGAIN		35	/* 再试一次 / try again */
#define SYS_MAP_SHARED		456
#define SYS_UNMAP_SHARED	457
#define SYS_PAGE_ALLOC		458
#define SYS_PAGE_FREE		459

#define IPC_INLINE_MAX	64
#define TEST_PORT	9
#define SHM_VA		0x0B000000u	/* 专用共享页 VA / dedicated shared-page VA */

/*
 * 中文：一条 IPC 消息的用户态镜像，布局须与内核 struct ipc_msg 一致。
 * EN: User-space mirror of an IPC message; layout must match the kernel.
 */
struct ipc_msg {
	unsigned	from;
	unsigned	type;
	unsigned	len;
	unsigned	pa;
	unsigned	reply_port;
	char		inline_data[IPC_INLINE_MAX];
};

/* 中文：通用 int $0x80 系统调用封装（最多 4 个参数） */
/* EN: Generic int $0x80 syscall wrapper (up to 4 arguments). */
static int
syscall4(int n, int a, int b, int c, int d)
{
	int r;

	__asm__ volatile("int $0x80"
	    : "=a"(r)
	    : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d)
	    : "memory");
	return (r);
}

#define port_register(p)	syscall4(SYS_PORT_REGISTER, (p),0,0,0)
#define port_release(p)		syscall4(SYS_PORT_RELEASE, (p),0,0,0)
#define ipc_send(p,m)		syscall4(SYS_SEND, (p),(int)(m),0,0)
#define ipc_recv(p,m)		syscall4(SYS_RECV, (p),(int)(m),0,0)

/*
 * 中文：轮询接收，直到拿到消息或超时（内核对用户线程返回 -EAGAIN）。
 * EN: Poll-receive until a message arrives or timeout (kernel returns
 * -EAGAIN to user threads).
 */
static int
recv_retry(int port, struct ipc_msg *m)
{
	int r, i;

	for (i = 0; i < 100000; i++) {
		r = ipc_recv(port, m);
		if (r != -EAGAIN)
			return (r);
	}
	return (-EAGAIN);
}
#define map_shared(pid,va,pa,pr) syscall4(SYS_MAP_SHARED,(pid),(va),(pa),(pr))
#define unmap_shared(pid,va)	syscall4(SYS_UNMAP_SHARED,(pid),(va),0,0)
#define page_alloc()		syscall4(SYS_PAGE_ALLOC,0,0,0,0)
#define page_free(pa)		syscall4(SYS_PAGE_FREE,(int)(pa),0,0,0)

/* 中文：打印一条失败信息并计数 */
/* EN: Print one failure line and count it. */
static int
fail(const char *what)
{

	printf("ipc: %s FAILED\n", what);
	return (1);
}

int
main(void)
{
	struct ipc_msg m, out;
	int bad = 0, st, pa;
	pid_t pid;
	char *p;

	/* ---- 1: 同进程消息往返 / same-process message round trip ---- */
	if (port_register(TEST_PORT) < 0)
		bad += fail("port_register");
	memset(&m, 0, sizeof(m));
	m.type = 0xC0DE;
	m.len = 4;
	strcpy(m.inline_data, "ping");
	if (ipc_send(TEST_PORT, &m) < 0)
		bad += fail("send");
	memset(&out, 0, sizeof(out));
	if (recv_retry(TEST_PORT, &out) < 0)
		bad += fail("recv");
	if (out.type != 0xC0DE || out.len != 4 ||
	    strcmp(out.inline_data, "ping") != 0)
		bad += fail("roundtrip payload");
	if (out.from == 0)
		bad += fail("kernel did not fill from");
	printf("ipc: roundtrip ok (from=%u)\n", out.from);

	/* ---- 2: 跨进程会合 / cross-process rendezvous ---- */
	pid = fork();
	if (pid < 0) {
		bad += fail("fork");
	} else if (pid == 0) {
		/* 子进程：注册端口并等待父进程消息 */
		struct ipc_msg cm;

		if (port_register(TEST_PORT + 1) < 0)
			_exit(2);
		memset(&cm, 0, sizeof(cm));
		if (recv_retry(TEST_PORT + 1, &cm) < 0)
			_exit(3);
		if (cm.type != 0x1234 || strcmp(cm.inline_data, "hi") != 0)
			_exit(4);
		_exit(0);
	} else {
		struct ipc_msg pm;

		/* 等子进程注册端口（忙等） / let the child register first (busy wait) */
		{
			volatile unsigned long d;

			for (d = 0; d < 2000000ul; d++)
				;
		}
		memset(&pm, 0, sizeof(pm));
		pm.type = 0x1234;
		pm.len = 2;
		strcpy(pm.inline_data, "hi");
		if (ipc_send(TEST_PORT + 1, &pm) < 0)
			bad += fail("cross-process send");
		wait4(pid, &st, 0, (struct rusage *)0);
		if ((st & 0xff00) != 0)
			bad += fail("cross-process child");
		printf("ipc: crossproc ok\n");
	}

	/* ---- 3: 共享页映射 / shared page mapping ---- */
	pa = (int)page_alloc();
	if (pa == 0) {
		bad += fail("page_alloc");
	} else {
		p = (char *)SHM_VA;
		/* 先把物理页映射到本进程用户 VA，再写入 */
		if (map_shared((unsigned)getpid(), SHM_VA, (unsigned)pa, 3) < 0) {
			bad += fail("map_shared");
		} else {
			p[0] = 0xA5;
			p[1] = 0x5A;
			/* 中文：char 在本机有符号，0xA5 读回是负值，须按 unsigned char 比较。
			 * EN: char is signed here, so 0xA5 reads back negative; compare as
			 * unsigned char to avoid a false failure. */
			if ((unsigned char)p[0] != 0xA5 ||
			    (unsigned char)p[1] != 0x5A)
				bad += fail("shared write/read");
			unmap_shared((unsigned)getpid(), SHM_VA);
			printf("ipc: sharedpage ok\n");
		}
		page_free(pa);
	}

	port_release(TEST_PORT);
	printf("ipc: %s\n", bad ? "FAIL" : "PASS");
	return (bad ? 1 : 0);
}

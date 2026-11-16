/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * aio.c - asynchronous half of the VFS I/O split (os.md mechanism 5).
 *
 * The VFS read/write entry points are shared: a synchronous kiocb runs the
 * backend in the caller, while KI_ASYNC queues the same kiocb to this worker
 * and returns -EIOCBQUEUED.  The worker runs the backend on its own kernel
 * stack and calls ki_complete() exactly once; a real port would plug the
 * block-device completion softirq in at that point.
 * VFS 读写入口是共用的：同步 kiocb 在调用者上下文执行后端，KI_ASYNC 则把
 * 同一个 kiocb 排给本 worker 并返回 -EIOCBQUEUED；worker 在自己的内核栈上
 * 执行后端并恰好调用一次 ki_complete()。真实移植可在这里接入块设备完成软中断。
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "fs/vfs.h"
#include "thread/kthread.h"

static struct kiocb	*aio_head, *aio_tail;
static struct kthread	*aio_worker_thread;
static unsigned		aio_queued, aio_done, aio_pending;

static void
aio_worker(void *arg)
{

	(void)arg;
	for (;;) {
		struct kiocb *ki;
		void (*cb)(struct kiocb *, int);
		int r;

		while (aio_head == 0)
			kthread_block();
		ki = aio_head;
		aio_head = ki->ki_next;
		if (aio_head == 0)
			aio_tail = 0;
		ki->ki_next = 0;
		aio_pending--;
		ki->ki_state = KI_ACTIVE;

		cb = ki->ki_complete;
		ki->ki_complete = 0;
		ki->ki_flags = KI_SYNC;
		r = (ki->ki_op == KI_READ) ?
		    vfs_read_sync(ki->ki_vp, ki->ki_off, ki->ki_buf,
		    ki->ki_len) :
		    vfs_write_sync(ki->ki_vp, ki->ki_off, ki->ki_buf,
		    ki->ki_len);
		ki->ki_result = r;
		ki->ki_state = KI_DONE;
		aio_done++;
		ki->ki_complete = cb;
		if (cb != 0)
			cb(ki, r);
	}
}

int
vfs_submit_async(struct kiocb *ki)
{

	if (ki == 0)
		return (-EINVAL);
	ki->ki_state = KI_QUEUED;
	ki->ki_next = 0;
	if (aio_tail != 0)
		aio_tail->ki_next = ki;
	else
		aio_head = ki;
	aio_tail = ki;
	aio_queued++;
	aio_pending++;
	if (aio_worker_thread != 0)
		kthread_wake(aio_worker_thread);
	return (EIOCBQUEUED);
}

void
vfs_aio_init(void)
{

	aio_head = aio_tail = 0;
	aio_queued = aio_done = aio_pending = 0;
	aio_worker_thread = kthread_spawn("aio", aio_worker, 0);
	if (aio_worker_thread == 0)
		printf("aio: no worker thread\n");
}

void
vfs_aio_stats(unsigned *queued, unsigned *done, unsigned *pending)
{

	*queued = aio_queued;
	*done = aio_done;
	*pending = aio_pending;
}

static volatile int aio_test_done;
static volatile int aio_test_result;

static void
aio_test_complete(struct kiocb *ki, int r)
{

	(void)ki;
	aio_test_result = r;
	aio_test_done = 1;
}

int
vfs_aio_selftest(void)
{
	struct vnode *vp = 0;
	struct kiocb sync_ki, async_ki;
	char sync_buf[64], async_buf[64];
	int i, sv, av;

	if (vfs_namei("/readme.txt", &vp) < 0 || vp == 0) {
		printf("aiotest: no /readme.txt\n");
		return (-1);
	}
	bzero(&sync_ki, sizeof(sync_ki));
	bzero(&async_ki, sizeof(async_ki));
	sync_ki.ki_vp = vp;
	sync_ki.ki_op = KI_READ;
	sync_ki.ki_flags = KI_SYNC;
	sync_ki.ki_off = 0;
	sync_ki.ki_buf = sync_buf;
	sync_ki.ki_len = 32;
	sv = vfs_read_sync(vp, 0, sync_buf, 32);

	async_ki.ki_vp = vp;
	async_ki.ki_op = KI_READ;
	async_ki.ki_flags = KI_ASYNC;
	async_ki.ki_off = 0;
	async_ki.ki_buf = async_buf;
	async_ki.ki_len = 32;
	async_ki.ki_complete = aio_test_complete;
	aio_test_done = 0;
	aio_test_result = -1;
	av = vfs_read_async(&async_ki);
	for (i = 0; i < 200 && !aio_test_done; i++)
		kthread_run();
	vn_put(vp);

	printf("aiotest: sync %d async submit %d result %d\n", sv, av,
	    aio_test_result);
	if (av != EIOCBQUEUED) {
		printf("aiotest: FAIL (async did not queue)\n");
		return (-1);
	}
	if (aio_test_result != sv || bcmp(sync_buf, async_buf,
	    (unsigned)(sv > 0 ? sv : 0)) != 0) {
		printf("aiotest: FAIL (data mismatch)\n");
		return (-1);
	}
	printf("aiotest: PASS\n");
	return (0);
}


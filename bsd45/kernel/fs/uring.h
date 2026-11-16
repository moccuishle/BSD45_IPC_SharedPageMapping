/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * uring.h - shared submission/completion ring ABI (os.md mechanism 5).
 *
 * One 4 KB page is mapped read/write into the submitting process and also
 * into the kernel.  Userspace writes SQEs and advances sq_tail; the kernel
 * consumes them, then writes CQEs and advances cq_tail.  Indices are free
 * running; the effective count is (tail - head) and the ring size is
 * URING_ENTRIES.
 * 一个 4 KB 页同时映射到提交进程和内核：用户写 SQE 并推进 sq_tail，内核
 * 消费后写 CQE 并推进 cq_tail。索引自由递增，有效数量为 tail-head，
 * 环大小为 URING_ENTRIES。
 */
#ifndef _URING_H_
#define _URING_H_

#define	URING_MAGIC	0x5552494e	/* "URIN" */
#define	URING_ENTRIES	8
#define	URING_OFF_HDR	0
#define	URING_OFF_SQ	64
#define	URING_OFF_CQ	(URING_OFF_SQ + URING_ENTRIES * 24)

struct uring_sqe {
	unsigned	sq_op;		/* 1 read, 2 write */
	unsigned	sq_fd;
	unsigned	sq_off;
	unsigned	sq_len;
	unsigned	sq_buf;		/* user buffer VA */
	unsigned	sq_user_data;
};

struct uring_cqe {
	unsigned	cq_user_data;
	int		cq_result;
	unsigned	cq_flags;
};

struct uring_header {
	unsigned	magic;
	unsigned	entries;
	unsigned	sq_head;	/* kernel consumer */
	unsigned	sq_tail;	/* user producer */
	unsigned	cq_head;	/* user consumer */
	unsigned	cq_tail;	/* kernel producer */
	unsigned	sq_dropped;
	unsigned	cq_overflow;
	unsigned	flags;
	unsigned	pad[7];
};

/* syscalls */
#define	SYS_URING_SETUP		463
#define	SYS_URING_ENTER		464
#define	SYS_URING_TEARDOWN	465

#endif /* !_URING_H_ */

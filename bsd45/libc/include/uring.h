/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * uring.h - userspace view of the shared SQ/CQ ring (keep in sync with
 * kernel/fs/uring.h).
 */
#ifndef _URING_USER_H_
#define _URING_USER_H_

#define	URING_MAGIC	0x5552494e
#define	URING_ENTRIES	8
#define	URING_OFF_HDR	0
#define	URING_OFF_SQ	64
#define	URING_OFF_CQ	(URING_OFF_SQ + URING_ENTRIES * 24)

struct uring_sqe {
	unsigned	sq_op;
	unsigned	sq_fd;
	unsigned	sq_off;
	unsigned	sq_len;
	unsigned	sq_buf;
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
	unsigned	sq_head;
	unsigned	sq_tail;
	unsigned	cq_head;
	unsigned	cq_tail;
	unsigned	sq_dropped;
	unsigned	cq_overflow;
	unsigned	flags;
	unsigned	pad[7];
};

#define	SYS_URING_SETUP		463
#define	SYS_URING_ENTER		464
#define	SYS_URING_TEARDOWN	465

#endif

/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * uring.c - shared SQ/CQ ring + batch submission (os.md mechanism 5).
 *
 * The ring page is mapped by syscall 463; this program fills four SQEs
 * (two reads and two writes), advances sq_tail once, and submits the whole
 * batch with one uring_enter(4,1).  It then consumes the CQEs.
 * 鍏变韩鐜〉鐢?463 鏄犲皠锛涚▼搴忎竴娆″～鍏?4 涓?SQE锛? 璇?2 鍐欙級锛屾帹杩涗竴娆? * sq_tail锛屽苟鐢ㄤ竴娆?uring_enter(4,1) 鎵归噺鎻愪氦锛岀劧鍚庢秷璐?CQE銆? */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <uring.h>

static int
sys_uring_setup(unsigned entries)
{
	int r;

	__asm__ volatile("int $0x80" : "=a"(r)
	    : "a"(SYS_URING_SETUP), "b"(entries) : "memory");
	return (r);
}

static int
sys_uring_enter(unsigned n, int wait)
{
	int r;

	__asm__ volatile("int $0x80" : "=a"(r)
	    : "a"(SYS_URING_ENTER), "b"(n), "c"(wait) : "memory");
	return (r);
}

static int
sys_uring_teardown(void)
{
	int r;

	__asm__ volatile("int $0x80" : "=a"(r)
	    : "a"(SYS_URING_TEARDOWN) : "memory");
	return (r);
}

int
main(void)
{
	struct uring_header *h;
	struct uring_sqe *sq;
	struct uring_cqe *cq;
	char rbuf[2][16], wbuf[2][16];
	int va, rfd, wfd, i, got, submitted, ok = 1;

	va = sys_uring_setup(URING_ENTRIES);
	if (va <= 0) {
		printf("uring: setup FAIL %d\n", va);
		return (1);
	}
	h = (struct uring_header *)va;
	sq = (struct uring_sqe *)(va + URING_OFF_SQ);
	cq = (struct uring_cqe *)(va + URING_OFF_CQ);
	rfd = open("/readme.txt", O_RDONLY);
	wfd = open("/tmp/uring", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (rfd < 0 || wfd < 0) {
		printf("uring: open FAIL\n");
		return (1);
	}
	strcpy(wbuf[0], "batch-write-0");
	strcpy(wbuf[1], "batch-write-1");

	for (i = 0; i < 2; i++) {
		sq[i].sq_op = 1;			/* read */
		sq[i].sq_fd = rfd;
		sq[i].sq_off = i * 16;
		sq[i].sq_len = 16;
		sq[i].sq_buf = (unsigned)rbuf[i];
		sq[i].sq_user_data = 0x100 + i;
	}
	for (i = 0; i < 2; i++) {
		sq[2 + i].sq_op = 2;		/* write */
		sq[2 + i].sq_fd = wfd;
		sq[2 + i].sq_off = i * 16;
		sq[2 + i].sq_len = 13;
		sq[2 + i].sq_buf = (unsigned)wbuf[i];
		sq[2 + i].sq_user_data = 0x200 + i;
	}
	h->sq_tail += 4;
	submitted = sys_uring_enter(4, 1);
	got = h->cq_tail - h->cq_head;
	printf("uring: submitted %d completions %d\n", submitted, got);
	if (submitted != 4 || got != 4)
		ok = 0;
	for (i = 0; i < got; i++) {
		struct uring_cqe *e = &cq[(h->cq_head + i) % h->entries];

		printf("uring: cqe user_data=%x result=%d\n",
		    e->cq_user_data, e->cq_result);
		if (e->cq_user_data < 0x100 || e->cq_result <= 0)
			ok = 0;
	}
	h->cq_head += got;

	/* read the two async writes back synchronously */
	if (lseek(wfd, 0, 0) < 0 || read(wfd, wbuf[0], 13) != 13)
		ok = 0;
	else if (strncmp(wbuf[0], "batch-write-0", 13) != 0)
		ok = 0;
	close(rfd);
	close(wfd);
	sys_uring_teardown();
	printf("uring: %s\n", ok ? "PASS" : "FAIL");
	return (ok ? 0 : 1);
}


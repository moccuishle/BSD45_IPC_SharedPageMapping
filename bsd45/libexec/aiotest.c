/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * aiotest.c - user-space VFS async I/O (os.md mechanism 5).
 *
 * syscall 460 submits a kiocb and returns a token; syscall 461 waits for
 * completion.  The same VFS read_iter/write_iter backend serves the
 * synchronous read/write calls, so this exercises the two-path split.
 * 系统调用 460 提交 kiocb 并返回 token；461 等待完成。同步 read/write 与
 * 异步请求共用同一套 VFS read_iter/write_iter 后端。
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define	KI_READ		1
#define	KI_WRITE	2

static int
aio_submit(int fd, int op, void *buf, unsigned len, unsigned off)
{
	int r;

	__asm__ volatile("int $0x80" : "=a"(r)
	    : "a"(460), "b"(fd), "c"(op), "d"(buf), "S"(len), "D"(off)
	    : "memory");
	return (r);
}

static int
aio_wait(int token)
{
	int r;

	__asm__ volatile("int $0x80" : "=a"(r)
	    : "a"(461), "b"(token) : "memory");
	return (r);
}

int
main(void)
{
	char buf[64], back[64];
	int fd, token, n, ok = 1;

	fd = open("/readme.txt", O_RDONLY);
	if (fd < 0) {
		printf("aiotest: open FAIL\n");
		return (1);
	}
	token = aio_submit(fd, KI_READ, buf, 32, 0);
	if (token <= 0) {
		printf("aiotest: submit FAIL %d\n", token);
		close(fd);
		return (1);
	}
	n = aio_wait(token);
	printf("aiotest: async read token=%d n=%d\n", token, n);
	if (n <= 0)
		ok = 0;
	close(fd);

	fd = open("/tmp/aio", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("aiotest: create FAIL\n");
		return (1);
	}
	token = aio_submit(fd, KI_WRITE, "async-data", 11, 0);
	n = (token > 0) ? aio_wait(token) : token;
	printf("aiotest: async write token=%d n=%d\n", token, n);
	if (n != 11)
		ok = 0;
	if (lseek(fd, 0, 0) < 0 || read(fd, back, 11) != 11) {
		printf("aiotest: readback FAIL\n");
		ok = 0;
	} else if (strncmp(back, "async-data", 11) != 0) {
		printf("aiotest: data mismatch [%s]\n", back);
		ok = 0;
	}
	close(fd);

	printf("aiotest: %s\n", ok ? "PASS" : "FAIL");
	return (ok ? 0 : 1);
}

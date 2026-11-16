/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * mmaptest(1) - S17 acceptance for mmap(2) + the file page cache.
 *
 * Three cases:
 *   1. MAP_PRIVATE of /readme.txt: the mapped bytes must equal what read(2)
 *      returns, and the mapping must be readable without a TRAP;
 *   2. anonymous MAP_ANON memory: write a pattern and read it back;
 *   3. MAP_SHARED of a tmpfs file: map it read-write, change a byte, msync(),
 *      then read the file back through read(2) and check the change.
 *
 * Each case prints one "mmap: ... ok" line; test_mmap.ps1 asserts them.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static int
fail(const char *what)
{

	printf("mmap: %s FAILED\n", what);
	return (1);
}

int
main(int argc, char **argv)
{
	struct stat sb;
	char ref[4096];
	char *p;
	int fd, n, bad = 0;

	(void)argc;
	(void)argv;

	/* ---- 1: private mapping of a read-only ISO9660 file ---- */
	fd = open("/readme.txt", O_RDONLY);
	if (fd < 0)
		return (fail("open /readme.txt"));
	if (fstat(fd, &sb) < 0 || sb.st_size <= 0 ||
	    sb.st_size > (int)sizeof(ref))
		return (fail("fstat /readme.txt"));
	n = read(fd, ref, sb.st_size);
	if (n != sb.st_size)
		return (fail("read /readme.txt"));
	p = (char *)mmap((caddr_t)0, (size_t)sb.st_size, PROT_READ,
	    MAP_PRIVATE, fd, (off_t)0);
	if (p == (char *)-1)
		return (fail("mmap /readme.txt"));
	if (memcmp(p, ref, (unsigned)sb.st_size) != 0)
		bad++;
	/* the mapped page is still readable as a plain pointer */
	if (p[0] < ' ' || p[0] > '~')
		bad++;
	/* do not print the size: it depends on the checkout's line endings */
	printf("mmap: file ok match=%d\n", bad == 0);
	if (bad)
		return (1);
	if (munmap((caddr_t)p, (size_t)sb.st_size) < 0)
		return (fail("munmap file"));
	close(fd);

	/* ---- 2: anonymous memory ---- */
	p = (char *)mmap((caddr_t)0, 8192, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, (off_t)0);
	if (p == (char *)-1)
		return (fail("mmap anon"));
	memset(p, 0, 8192);
	strcpy(p, "anon-hello");
	p[4096 + 5] = 'X';
	if (strcmp(p, "anon-hello") != 0 || p[4096 + 5] != 'X')
		return (fail("anon contents"));
	printf("mmap: anon ok\n");
	if (munmap((caddr_t)p, 8192) < 0)
		return (fail("munmap anon"));

	/* ---- 3: shared writable mapping of a tmpfs file ---- */
	fd = open("/tmp/mm", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return (fail("open /tmp/mm"));
	if (write(fd, "shared-data", 11) != 11)
		return (fail("write /tmp/mm"));
	if (lseek(fd, 0, 0) < 0)
		return (fail("lseek /tmp/mm"));
	p = (char *)mmap((caddr_t)0, 4096, PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, (off_t)0);
	if (p == (char *)-1)
		return (fail("mmap /tmp/mm"));
	if (memcmp(p, "shared-data", 11) != 0)
		return (fail("shared contents"));
	p[0] = 'S';			/* 's' -> 'S' */
	if (msync((caddr_t)p, 4096) < 0)
		return (fail("msync /tmp/mm"));
	memset(ref, 0, sizeof(ref));
	if (lseek(fd, 0, 0) < 0)
		return (fail("lseek /tmp/mm#2"));
	if (read(fd, ref, 11) != 11)
		return (fail("read /tmp/mm"));
	if (strncmp(ref, "Shared-data", 11) != 0) {
		printf("mmap: shared readback=[%s]\n", ref);
		return (fail("msync contents"));
	}
	printf("mmap: shared ok\n");
	if (munmap((caddr_t)p, 4096) < 0)
		return (fail("munmap shared"));
	close(fd);

	printf("mmap: ok\n");
	return (0);
}

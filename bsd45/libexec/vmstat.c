/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * vmstat(1) - cat the S17 cache counters exposed as /dev/vmstat.
 *
 * The pseudo file is rendered on every read (the counters are live), so a
 * simple open/read/write loop is enough.  Lines are:
 *
 *	vnode: total .. used .. free ..
 *	buf:   total .. used .. hits .. misses .. reads .. readahead .. dirty ..
 *	name:  total .. used .. hits .. misses ..
 *	page:  total .. used .. hits .. misses .. fills .. evicts ..
 *	pages: alloc .. freed .. inuse ..
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	char buf[256];
	int fd, n;

	(void)argc;
	(void)argv;
	fd = open("/dev/vmstat", O_RDONLY);
	if (fd < 0) {
		printf("vmstat: /dev/vmstat: cannot open\n");
		return (1);
	}
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		write(1, buf, n);
	close(fd);
	return (0);
}

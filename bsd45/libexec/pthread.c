/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * pthreadtest.c - user-space pthread_create/pthread_join smoke test.
 */
#include <stdio.h>
#include <pthread.h>

static volatile int counter;

static void *
worker(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < 1000; i++)
		counter++;
	return ((void *)42);
}

int
main(void)
{
	pthread_t t[4], d;
	void *ret;
	int i, ok = 1;

	counter = 0;
	for (i = 0; i < 4; i++)
		if (pthread_create(&t[i], 0, worker, 0) < 0) {
			printf("pthreadtest: create %d FAIL\n", i);
			return (1);
		}
	printf("pthreadtest: created 4 threads, self=%d\n",
	    (int)pthread_self());
	for (i = 0; i < 4; i++) {
		if (pthread_join(t[i], &ret) < 0 || (int)ret != 42)
			ok = 0;
	}
	printf("pthreadtest: counter=%d join=%s\n", counter,
	    ok ? "ok" : "FAIL");
	if (!ok || counter != 4000) {
		printf("pthreadtest: FAIL\n");
		return (1);
	}

	/* detached thread: nobody joins it, it must still run to exit */
	if (pthread_create(&d, 0, worker, 0) < 0 ||
	    pthread_detach(d) < 0) {
		printf("pthreadtest: detach create FAIL\n");
		return (1);
	}
	for (i = 0; i < 20000; i++)
		pthread_yield();
	printf("pthreadtest: detach counter=%d\n", counter);
	if (counter != 5000) {
		printf("pthreadtest: detach FAIL\n");
		return (1);
	}
	printf("pthreadtest: PASS\n");
	return (0);
}

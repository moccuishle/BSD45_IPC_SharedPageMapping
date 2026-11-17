/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * pthread.h - user-space threads backed by BSD45 kernel kthreads (S26).
 */
#ifndef _PTHREAD_H_
#define _PTHREAD_H_

typedef int pthread_t;
typedef int pthread_attr_t;

#ifdef __cplusplus
extern "C" {
#endif

int	pthread_create(pthread_t *tid, const pthread_attr_t *attr,
	    void *(*start)(void *), void *arg);
int	pthread_join(pthread_t tid, void **retval);
int	pthread_detach(pthread_t tid);
pthread_t pthread_self(void);
void	pthread_exit(void *retval);
int	pthread_yield(void);
/* scheduling: 0 = lowest, 19 = highest */
int	sched_setprio(int prio);
int	sched_getprio(void);

#ifdef __cplusplus
}
#endif

#endif

/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * clock.h - PIT/tick interface.
 */
#ifndef _CLOCK_H_
#define _CLOCK_H_

struct sysregs;

void	clock_init(void);
void	clock_irq(struct sysregs *r);
void	clock_print(void);
void	clock_lat_print(void);

/* arch backend hooks */
void	hal_clock_init(void);
void	hal_clock_eoi(void);
unsigned hal_rdtsc(void);
unsigned clock_get_ticks(void);
unsigned clock_quantum_get(void);

#endif /* !_CLOCK_H_ */

/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kgdb_glue.c	8.1 (Berkeley) 6/11/93
 */

/*
 * This file must be compiled with gcc -fno-defer-pop.
 */

#ifdef KGDB

#include <sys/param.h>
#include <machine/frame.h>
#include <machine/reg.h>

#ifndef lint
static char rcsid[] =
    "@(#) $Header: /u/donn/c/gdb/kernel/RCS/kgdb_glue.c,v 1.2 91/03/31 16:04:52 donn Exp Locker: donn $ (LBL)";
#endif

#define KGDB_STACKSIZE 0x800
#define KGDB_STACKWORDS (KGDB_STACKSIZE / sizeof(u_long))

u_long kgdb_stack[KGDB_STACKWORDS];

#define getsp(v) ((v) = cpu_read_esp())
#define setsp(v) cpu_write_esp(v)

/* EN: Copies nbytes from src to dst a u_long at a time, plus a trailing u_short when nbytes & 2; the do-while copies at least one word even for tiny sizes. */
/* 中文：以 u_long 为单位把 nbytes 字节从 src 拷到 dst，nbytes 余 2 时再补一个 u_short；do-while 保证极小长度也至少复制一个字。 */
static inline void
copywords(src, dst, nbytes)
	register u_long *src, *dst;
	register u_int nbytes;
{
	u_long *limit = src + (nbytes / sizeof(u_long));

	do {
		*dst++ = *src++;
	} while (src < limit);
	if (nbytes & 2)
		*(u_short *)dst = *(u_short *)src;
}

/* EN: Kernel-trap fixup: fixes tf_isp, copies the trap frame to kgdb_stack at splhigh and calls kgdb_trap; if it returns 1 the frame is copied back and kgdb_return_to_user runs. */
/* 中文：内核陷阱适配：计算帧长并修正 tf_isp，在 splhigh 下把陷阱帧复制到 kgdb_stack 并调用 kgdb_trap；返回 1 时把改后的帧拷回并进入 kgdb_return_to_user。 */
kgdb_trap_glue(frame)
	struct trapframe frame;
{
	u_long osp, nsp;
	u_int s;

	/*
	 * On internal traps, the hardware doesn't push ss and esp.
	 */
	u_int fsize = sizeof frame - 2 * sizeof (int);
#ifdef DONN
	extern int kgdb_debug;

	if (kgdb_debug > 3)
		pg("kgdb_trap_glue: entered, fsize = %d\n", fsize);
#endif

	/*
	 * After a kernel mode trap, the saved sp doesn't point to the right
	 * place.  The correct value is the top of the frame.
	 */
	frame.tf_isp = (u_long)(&frame) + fsize;

	/*
	 * Copy the interrupt context and frame to the new stack.
	 * We're throwing away trap()'s frame since we're going to do
	 * our own iret.
	 */
	nsp = (u_long)(&kgdb_stack[KGDB_STACKWORDS]) - fsize;

	copywords((u_long *)&frame, (u_long *)nsp, fsize);

	s = splhigh();

	getsp(osp);
	setsp(nsp);

	if (kgdb_trap(frame.tf_trapno, (struct frame *)nsp) == 0) {
		/*
		 * Get back on kernel stack.  This thread of control
		 * will return back up through trap().  If kgdb_trap()
		 * returns 0, it didn't handle the trap at all so
		 * the stack is still intact and everything will
		 * unwind okay from here up.
		 */
		setsp(osp);
		splx(s);
		return 0;
	}

	/*
	 * Copy back context, which has possibly changed.  Even the
	 * sp might have changed.
	 */
	osp = ((struct trapframe *)nsp)->tf_isp - fsize;
	copywords((u_long *)nsp, (u_long *)osp, fsize);
	setsp(osp);

	/*
	 * Restore the possible new context from frame.
	 */
	kgdb_return_to_user();
}

int kgdb_testval;

/* EN: Debug test helper: increments the global kgdb_testval and returns i + 1, used to exercise gdb breakpoints and expression evaluation. */
/* 中文：调试测试辅助：递增全局变量 kgdb_testval 并返回 i + 1，用于验证 gdb 断点与表达式求值。 */
kgdb_test(i)
{
        ++kgdb_testval;
        return i + 1;
}

#endif

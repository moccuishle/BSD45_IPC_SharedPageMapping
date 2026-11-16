/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	@(#)swapgeneric.c	8.1 (Berkeley) 6/16/93
 */

#include <machine/pte.h>

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/vm.h>
#include <sys/systm.h>
#include <sys/reboot.h>

/*
 * Generic configuration;  all in one
 */
dev_t	rootdev = makedev(0,0);
dev_t	dumpdev = makedev(0,1);
int	nswap;
struct	swdevt swdevt[] = {
	{ 1,	0,	0 },
	{ NODEV,	1,	0 },
};
long	dumplo;
int	dmmin, dmmax, dmtext;

extern	struct driver wddriver;

struct	genericconf {
	caddr_t	gc_driver;
	char	*gc_name;
	dev_t	gc_root;
} genericconf[] = {
	{ (caddr_t)&wddriver,	"wd",	makedev(0, 0),	},
	{ 0 },
};

/* EN: Its entire body sits inside #ifdef notdef, so this build yields an empty function: it never prompts for a root device and never sets rootdev, dumpdev or swap devices. */
/* 中文：整个函数体位于 #ifdef notdef 内，因此本次构建得到空函数：既不询问根设备，也不设置 rootdev、dumpdev 或交换设备。 */
setconf()
{
#ifdef notdef
	register struct genericconf *gc;
	int unit, swaponroot = 0;

	if (rootdev != NODEV)
		goto doswap;
	if (boothowto & RB_ASKNAME) {
		char name[128];
retry:
		printf("root device? ");
		gets(name);
		for (gc = genericconf; gc->gc_driver; gc++)
			if (gc->gc_name[0] == name[0] &&
			    gc->gc_name[1] == name[1])
				goto gotit;
		goto bad;
gotit:
		if (name[3] == '*') {
			name[3] = name[4];
			swaponroot++;
		}
		if (name[2] >= '0' && name[2] <= '7' && name[3] == 0) {
			unit = name[2] - '0';
			goto found;
		}
		printf("bad/missing unit number\n");
bad:
		printf("use dk%%d\n");
		goto retry;
	}
	unit = 0;
	for (gc = genericconf; gc->gc_driver; gc++) {
		for (ui = vbdinit; ui->ui_driver; ui++) {
			if (ui->ui_alive == 0)
				continue;
			if (ui->ui_unit == 0 && ui->ui_driver ==
			    (struct vba_driver *)gc->gc_driver) {
				printf("root on %s0\n",
				    ui->ui_driver->ud_dname);
				goto found;
			}
		}
	}
	printf("no suitable root\n");
	cpu_halt();
found:
	gc->gc_root = makedev(major(gc->gc_root), unit*8);
	rootdev = gc->gc_root;
doswap:
	swdevt[0].sw_dev = argdev = dumpdev =
	    makedev(major(rootdev), minor(rootdev)+1);
	/* swap size and dumplo set during autoconfigure */
	if (swaponroot)
		rootdev = dumpdev;
#endif
}

/* EN: Reads a console line character by character with cngetc(), echoing as it goes and handling CR/LF, backspace/DEL, '#' as delete, '@' or ^U as line kill, then NUL-terminates cp. */
/* 中文：通过 cngetc() 逐字符读取控制台输入并回显，处理回车换行、退格/DEL、'#' 删除一字符、'@' 或 ^U 整行删除，最后在 cp 末尾写入 NUL。 */
gets(cp)
	char *cp;
{
	register char *lp;
	register c;

	lp = cp;
	for (;;) {
		printf("%c", c = cngetc()&0177);
		switch (c) {
		case '\n':
		case '\r':
			*lp++ = '\0';
			return;
		case '\b':
		case '\177':
			if (lp > cp) {
				printf(" \b");
				lp--;
			}
			continue;
		case '#':
			lp--;
			if (lp < cp)
				lp = cp;
			continue;
		case '@':
		case 'u'&037:
			lp = cp;
			printf("%c", '\n');
			continue;
		default:
			*lp++ = c;
		}
	}
}

/*
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)subr_xxx.c	8.3 (Berkeley) 3/29/95
 */

/*
 * Miscellaneous trivial functions, including many
 * that are often inline-expanded or done in assembler.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <machine/cpu.h>

/*
 * Unsupported device function (e.g. writing to read-only device).
 */
/* 中文：不支持的设备操作桩函数，返回 ENODEV（无此设备）错误 */
/* EN: Stub for unsupported device operations, returning ENODEV (no such device) */
int
enodev()
{

	return (ENODEV);
}

/*
 * Unconfigured device function; driver not configured.
 */
/* 中文：未配置设备桩函数，返回 ENXIO（设备未配置）错误 */
/* EN: Stub for an unconfigured device, returning ENXIO (device not configured) */
int
enxio()
{

	return (ENXIO);
}

/*
 * Unsupported ioctl function.
 */
/* 中文：不支持的 ioctl 桩函数，返回 ENOTTY（不适用的 ioctl）错误 */
/* EN: Stub for unsupported ioctl, returning ENOTTY (inappropriate ioctl) */
int
enoioctl()
{

	return (ENOTTY);
}

/*
 * Unsupported system function.
 * This is used for an otherwise-reasonable operation
 * that is not supported by the current system binary.
 */
/* 中文：不支持的系统调用桩函数，返回 ENOSYS（功能未实现）错误 */
/* EN: Stub for an unsupported system function, returning ENOSYS (function not implemented) */
int
enosys()
{

	return (ENOSYS);
}

/*
 * Return error for operation not supported
 * on a specific object or file type.
 */
/* 中文：对特定对象或文件类型不支持的操作，返回 EOPNOTSUPP 错误 */
/* EN: Stub for an operation not supported on a given object/file type, returning EOPNOTSUPP */
int
eopnotsupp()
{

	return (EOPNOTSUPP);
}

/*
 * Return error for an inval operation
 * on a specific object or file type.
 */
/* 中文：对特定对象或文件类型的无效操作，返回 EINVAL（无效参数）错误 */
/* EN: Stub for an invalid operation on a given object/file type, returning EINVAL (invalid argument) */
int
einval()
{

	return (EINVAL);
}

/*
 * Generic null operation, always returns success.
 */
/* 中文：通用空操作，总是返回成功（0） */
/* EN: Generic null operation that always returns success (0) */
int
nullop()
{

	return (0);
}

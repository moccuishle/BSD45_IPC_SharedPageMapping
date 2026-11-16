/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * kstr.h - kernel string abstraction with a compile-time character set.
 *
 * KERNEL_UNICODE=1 builds every kernel string as UTF-16LE (NT style,
 * Length/MaximumLength in bytes); KERNEL_UNICODE=0 builds them as plain
 * ASCII bytes.  All kernel code uses struct kstr and the kstr_* API, so the
 * two variants share one source; only this header picks the representation.
 *
 * The user/kernel boundary always speaks UTF-8 (A version): kstr_set_utf8()
 * converts once on entry and rejects non-ASCII input in the ASCII build.
 * Printing is the other boundary: kstr_to_utf8() before printf().
 */
#ifndef _OBJ_KSTR_H_
#define _OBJ_KSTR_H_

#ifdef KERNEL_UNICODE

#define	KSTR_UTF16	1
struct kstr {
	unsigned short	len;		/* bytes */
	unsigned short	maxlen;		/* bytes */
	unsigned short	*buf;		/* UTF-16LE units */
};

#else

#define	KSTR_UTF16	0
struct kstr {
	unsigned	len;		/* bytes */
	unsigned	maxlen;		/* bytes */
	char		*buf;
};

#endif

void	kstr_init(struct kstr *s, void *buf, unsigned maxbytes);
void	kstr_clear(struct kstr *s);

/*
 * A API: UTF-8/ASCII at the user boundary (always present).
 * W API: native UTF-16 units (compiled only when KERNEL_UNICODE=1).
 */
int	kstr_set_a(struct kstr *s, const char *utf8);
int	kstr_to_a(const struct kstr *s, char *out, unsigned outlen);
#if KSTR_UTF16
int	kstr_set_w(struct kstr *s, const unsigned short *units,
	    unsigned nbytes);
int	kstr_to_w(const struct kstr *s, unsigned short *out,
	    unsigned maxbytes);
#define	KSTR_HAS_W	1
#else
#define	KSTR_HAS_W	0
#endif

/* historical names kept as aliases of the A entry points */
#define	kstr_set_utf8	kstr_set_a
#define	kstr_to_utf8	kstr_to_a
int	kstr_cmp(const struct kstr *a, const struct kstr *b);
int	kstr_icmp(const struct kstr *a, const struct kstr *b);
unsigned kstr_hash(const struct kstr *s);
int	kstr_eq_ascii(const struct kstr *s, const char *ascii);
int	kstr_copy(struct kstr *dst, const struct kstr *src);
int	kstr_append(struct kstr *dst, const struct kstr *src);
int	kstr_append_ascii(struct kstr *dst, const char *ascii);
int	kstr_prefix(const struct kstr *prefix, const struct kstr *path);

/* unit-at-a-time helpers for path parsing (UTF-16: 16-bit, ASCII: 8-bit) */
unsigned kstr_units(const struct kstr *s);
unsigned kstr_unit(const struct kstr *s, unsigned i);
int	kstr_push_unit(struct kstr *s, unsigned c);

#endif

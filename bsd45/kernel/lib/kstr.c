/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * kstr.c - kernel string implementation for KERNEL_UNICODE=0/1 (S19).
 *
 * Both variants expose the same kstr_* API.  The UTF-16 build accepts full
 * UTF-8 at the boundary (including surrogate pairs); the ASCII build rejects
 * non-ASCII input, which keeps "ASCII kernel" builds honest.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "lib/kstr.h"

extern void bcopy(const void *, void *, unsigned);
extern unsigned strlen(const char *);

#ifdef KERNEL_UNICODE

/* ------------------------------------------------------------- UTF-16LE */

/* 中文：（UTF-16 构建）初始化 kstr：绑定缓冲区、清零长度并写入结尾 NUL */
/* EN: (UTF-16 build) Initialize a kstr by binding a buffer, zeroing its length and writing the terminating NUL */
void
kstr_init(struct kstr *s, void *buf, unsigned maxbytes)
{

	s->buf = (unsigned short *)buf;
	s->len = 0;
	s->maxlen = (unsigned short)maxbytes;
	if (s->buf != 0 && maxbytes >= 2)
		s->buf[0] = 0;
}

/* 中文：（UTF-16 构建）清空 kstr：长度置零并写入结尾 NUL */
/* EN: (UTF-16 build) Clear a kstr by zeroing its length and writing the terminating NUL */
void
kstr_clear(struct kstr *s)
{

	s->len = 0;
	if (s->buf != 0 && s->maxlen >= 2)
		s->buf[0] = 0;
}

/* 中文：（UTF-16 构建）在 kstr 末尾追加一个 UTF-16 单元，检查剩余空间并维持结尾 NUL */
/* EN: (UTF-16 build) Append one UTF-16 unit to a kstr, checking remaining space and keeping the terminating NUL */
static int
kstr_put(struct kstr *s, unsigned short c)
{

	if ((unsigned)s->len + 2 > (unsigned)s->maxlen - 2)
		return (-1);
	s->buf[s->len / 2] = c;
	s->len += 2;
	s->buf[s->len / 2] = 0;
	return (0);
}

/* 中文：（UTF-16 构建）把 UTF-8 字符串解码成 UTF-16 存入 kstr（含代理对），非法序列或空间不足返回 -1 */
/* EN: (UTF-16 build) Decode a UTF-8 string into UTF-16 in the kstr (including surrogate pairs), rejecting invalid sequences or insufficient space */
int
kstr_set_a(struct kstr *s, const char *utf8)
{
	const unsigned char *p = (const unsigned char *)utf8;
	unsigned cp, need, i;

	kstr_clear(s);
	if (utf8 == 0)
		return (0);
	while (*p != 0) {
		if (*p < 0x80) {
			cp = *p++;
			need = 0;
		} else if ((*p & 0xe0) == 0xc0) {
			cp = *p++ & 0x1f;
			need = 1;
			if (cp < 2)
				return (-1);
		} else if ((*p & 0xf0) == 0xe0) {
			cp = *p++ & 0x0f;
			need = 2;
		} else if ((*p & 0xf8) == 0xf0) {
			cp = *p++ & 0x07;
			need = 3;
		} else
			return (-1);
		for (i = 0; i < need; i++) {
			if ((*p & 0xc0) != 0x80)
				return (-1);
			cp = (cp << 6) | (*p++ & 0x3f);
		}
		if (cp >= 0xd800 && cp <= 0xdfff)
			return (-1);
		if (cp < 0x10000) {
			if (kstr_put(s, (unsigned short)cp) < 0)
				return (-1);
		} else {
			cp -= 0x10000;
			if (kstr_put(s, (unsigned short)(0xd800 |
			    ((cp >> 10) & 0x3ff))) < 0)
				return (-1);
			if (kstr_put(s, (unsigned short)(0xdc00 |
			    (cp & 0x3ff))) < 0)
				return (-1);
		}
	}
	return (0);
}

/* 中文：（UTF-16 构建）把一个 Unicode 码点按 UTF-8 编码写入输出缓冲区 */
/* EN: (UTF-16 build) Encode one Unicode code point as UTF-8 into the output buffer */
static int
kstr_utf8_put(char *out, unsigned outlen, unsigned n, unsigned cp)
{

	if (n + 1 > outlen)
		return (-1);
	if (cp < 0x80) {
		out[n++] = (char)cp;
	} else if (cp < 0x800) {
		out[n++] = (char)(0xc0 | (cp >> 6));
		out[n++] = (char)(0x80 | (cp & 0x3f));
	} else if (cp < 0x10000) {
		out[n++] = (char)(0xe0 | (cp >> 12));
		out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[n++] = (char)(0x80 | (cp & 0x3f));
	} else {
		out[n++] = (char)(0xf0 | (cp >> 18));
		out[n++] = (char)(0x80 | ((cp >> 12) & 0x3f));
		out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[n++] = (char)(0x80 | (cp & 0x3f));
	}
	return ((int)n);
}

/* 中文：（UTF-16 构建）把 kstr 中的 UTF-16 内容（含代理对）转码成 UTF-8 字符串 */
/* EN: (UTF-16 build) Transcode the UTF-16 content of a kstr (including surrogate pairs) into a UTF-8 string */
int
kstr_to_a(const struct kstr *s, char *out, unsigned outlen)
{
	unsigned i, n = 0;

	if (outlen == 0)
		return (-1);
	for (i = 0; i + 1 < (unsigned)s->len; i += 2) {
		unsigned cp = s->buf[i / 2];

		if (cp >= 0xd800 && cp <= 0xdbff && i + 3 < s->len) {
			unsigned lo = s->buf[i / 2 + 1];

			if (lo >= 0xdc00 && lo <= 0xdfff) {
				cp = 0x10000 + ((cp - 0xd800) << 10) +
				    (lo - 0xdc00);
				i += 2;
			}
		}
		n = kstr_utf8_put(out, outlen, n, cp);
		if ((int)n < 0)
			return (-1);
	}
	out[n] = 0;
	return ((int)n);
}

/*
 * W API: native UTF-16 units, only compiled in the Unicode build.  This is
 * what internal callers use when they already hold UTF-16 (no conversion).
 */
/* 中文：（UTF-16 构建）直接复制原生 UTF-16 单元到 kstr，不做转换 */
/* EN: (UTF-16 build) Set a kstr by copying native UTF-16 units directly without conversion */
int
kstr_set_w(struct kstr *s, const unsigned short *units, unsigned nbytes)
{

	if (units == 0) {
		kstr_clear(s);
		return (0);
	}
	if (nbytes + 2 > s->maxlen)
		return (-1);
	bcopy(units, s->buf, nbytes);
	s->len = (unsigned short)nbytes;
	s->buf[s->len / 2] = 0;
	return (0);
}

/* 中文：（UTF-16 构建）把 kstr 的 UTF-16 内容复制到输出数组并补上结尾 NUL */
/* EN: (UTF-16 build) Copy the UTF-16 content of a kstr into an output array with a terminating NUL */
int
kstr_to_w(const struct kstr *s, unsigned short *out, unsigned maxbytes)
{

	if (out == 0 || (unsigned)s->len + 2 > maxbytes)
		return (-1);
	bcopy(s->buf, out, s->len);
	out[s->len / 2] = 0;
	return ((int)s->len);
}

/* 中文：（UTF-16 构建）按 UTF-16 单元逐个比较两个 kstr */
/* EN: (UTF-16 build) Compare two kstrs unit by unit as UTF-16 values */
int
kstr_cmp(const struct kstr *a, const struct kstr *b)
{
	unsigned i, n;

	n = (a->len < b->len) ? a->len : b->len;
	for (i = 0; i < n; i += 2)
		if (a->buf[i / 2] != b->buf[i / 2])
			return ((int)a->buf[i / 2] - (int)b->buf[i / 2]);
	return ((int)a->len - (int)b->len);
}

/* 中文：（UTF-16 构建）把大写 ASCII 字符折叠为小写（作用于一个 UTF-16 单元） */
/* EN: (UTF-16 build) Fold an uppercase ASCII character to lowercase within one UTF-16 unit */
static unsigned short
kstr_fold(unsigned short c)
{

	if (c >= 'A' && c <= 'Z')
		return ((unsigned short)(c - 'A' + 'a'));
	return (c);
}

/* 中文：（UTF-16 构建）忽略 ASCII 大小写比较两个 kstr */
/* EN: (UTF-16 build) Compare two kstrs case-insensitively for ASCII characters */
int
kstr_icmp(const struct kstr *a, const struct kstr *b)
{
	unsigned i, n;
	unsigned short ca, cb;

	n = (a->len < b->len) ? a->len : b->len;
	for (i = 0; i < n; i += 2) {
		ca = kstr_fold(a->buf[i / 2]);
		cb = kstr_fold(b->buf[i / 2]);
		if (ca != cb)
			return ((int)ca - (int)cb);
	}
	return ((int)a->len - (int)b->len);
}

/* 中文：（UTF-16 构建）对折叠后的 UTF-16 单元计算 kstr 的 FNV 哈希 */
/* EN: (UTF-16 build) Compute the FNV hash of a kstr over its folded UTF-16 units */
unsigned
kstr_hash(const struct kstr *s)
{
	unsigned i, h = 2166136261u;

	for (i = 0; i < s->len; i += 2) {
		h ^= kstr_fold(s->buf[i / 2]);
		h *= 16777619u;
	}
	return (h);
}

/* 中文：（UTF-16 构建）判断 kstr 内容是否与给定 ASCII 字符串相等 */
/* EN: (UTF-16 build) Test whether a kstr's content equals a given ASCII string */
int
kstr_eq_ascii(const struct kstr *s, const char *ascii)
{
	unsigned i, n = strlen(ascii);

	if (s->len != n * 2)
		return (0);
	for (i = 0; i < n; i++)
		if (s->buf[i] != (unsigned char)ascii[i])
			return (0);
	return (1);
}

/* 中文：（UTF-16 构建）把一个 kstr 复制到另一个，先检查目标容量是否够 */
/* EN: (UTF-16 build) Copy one kstr into another after checking that the destination has enough capacity */
int
kstr_copy(struct kstr *dst, const struct kstr *src)
{

	if (src->len + 2 > dst->maxlen)
		return (-1);
	bcopy(src->buf, dst->buf, src->len);
	dst->len = src->len;
	dst->buf[dst->len / 2] = 0;
	return (0);
}

/* 中文：（UTF-16 构建）把源 kstr 追加到目标 kstr 的末尾 */
/* EN: (UTF-16 build) Append a source kstr to the end of a destination kstr */
int
kstr_append(struct kstr *dst, const struct kstr *src)
{
	unsigned i;

	if ((unsigned)dst->len + (unsigned)src->len + 2 > dst->maxlen)
		return (-1);
	for (i = 0; i < src->len; i += 2)
		dst->buf[(dst->len + i) / 2] = src->buf[i / 2];
	dst->len += src->len;
	dst->buf[dst->len / 2] = 0;
	return (0);
}

/* 中文：（UTF-16 构建）把 ASCII 字符串追加到 kstr 的末尾 */
/* EN: (UTF-16 build) Append an ASCII string to the end of a kstr */
int
kstr_append_ascii(struct kstr *dst, const char *ascii)
{
	unsigned n = strlen(ascii), i;

	if ((unsigned)dst->len + n * 2 + 2 > dst->maxlen)
		return (-1);
	for (i = 0; i < n; i++)
		dst->buf[(dst->len + i * 2) / 2] = (unsigned char)ascii[i];
	dst->len += n * 2;
	dst->buf[dst->len / 2] = 0;
	return (0);
}

/* 中文：（UTF-16 构建）判断一个 kstr 是否是另一个的前缀（忽略 ASCII 大小写） */
/* EN: (UTF-16 build) Test whether one kstr is a prefix of another, ignoring ASCII case */
int
kstr_prefix(const struct kstr *prefix, const struct kstr *path)
{

	if (prefix->len > path->len)
		return (0);
	if (prefix->len == 0)
		return (1);
	return (kstr_icmp(prefix, path) == 0);
}

/* 中文：（UTF-16 构建）返回 kstr 中的 UTF-16 单元个数 */
/* EN: (UTF-16 build) Return the number of UTF-16 units in a kstr */
unsigned
kstr_units(const struct kstr *s)
{

	return ((unsigned)s->len / 2);
}

/* 中文：（UTF-16 构建）返回指定下标的 UTF-16 单元，下标越界时返回 0 */
/* EN: (UTF-16 build) Return the UTF-16 unit at a given index, or 0 when out of range */
unsigned
kstr_unit(const struct kstr *s, unsigned i)
{

	if (i >= kstr_units(s))
		return (0);
	return (s->buf[i]);
}

/* 中文：（UTF-16 构建）把一个 UTF-16 单元追加到 kstr 的末尾 */
/* EN: (UTF-16 build) Push one UTF-16 unit onto the end of a kstr */
int
kstr_push_unit(struct kstr *s, unsigned c)
{

	return (kstr_put(s, (unsigned short)c));
}

#else /* !KERNEL_UNICODE */

/* ---------------------------------------------------------------- ASCII */

/* 中文：（ASCII 构建）初始化 kstr：绑定字节缓冲区、清零长度并写入结尾 NUL */
/* EN: (ASCII build) Initialize a kstr by binding a byte buffer, zeroing its length and writing the terminating NUL */
void
kstr_init(struct kstr *s, void *buf, unsigned maxbytes)
{

	s->buf = (char *)buf;
	s->len = 0;
	s->maxlen = maxbytes;
	if (s->buf != 0 && maxbytes >= 1)
		s->buf[0] = 0;
}

/* 中文：（ASCII 构建）清空 kstr：长度置零并写入结尾 NUL */
/* EN: (ASCII build) Clear a kstr by zeroing its length and writing the terminating NUL */
void
kstr_clear(struct kstr *s)
{

	s->len = 0;
	if (s->buf != 0 && s->maxlen >= 1)
		s->buf[0] = 0;
}

/* 中文：（ASCII 构建）用 ASCII 字符串设置 kstr，遇到非 ASCII 字节则返回 -1 */
/* EN: (ASCII build) Set a kstr from an ASCII string, returning -1 on any non-ASCII byte */
int
kstr_set_a(struct kstr *s, const char *utf8)
{
	unsigned i, n;

	kstr_clear(s);
	if (utf8 == 0)
		return (0);
	n = strlen(utf8);
	if (n + 1 > s->maxlen)
		return (-1);
	for (i = 0; i < n; i++) {
		if ((unsigned char)utf8[i] >= 0x80)
			return (-1);	/* ASCII build */
		s->buf[i] = utf8[i];
	}
	s->buf[n] = 0;
	s->len = n;
	return (0);
}

/* 中文：（ASCII 构建）把 kstr 的 ASCII 内容复制到输出缓冲区并补上结尾 NUL */
/* EN: (ASCII build) Copy the ASCII content of a kstr into an output buffer with a terminating NUL */
int
kstr_to_a(const struct kstr *s, char *out, unsigned outlen)
{

	if (s->len + 1 > outlen)
		return (-1);
	bcopy(s->buf, out, s->len);
	out[s->len] = 0;
	return ((int)s->len);
}

/* 中文：（ASCII 构建）按字节比较两个 ASCII kstr */
/* EN: (ASCII build) Compare two ASCII kstrs byte by byte */
int
kstr_cmp(const struct kstr *a, const struct kstr *b)
{
	unsigned i, n;

	n = (a->len < b->len) ? a->len : b->len;
	for (i = 0; i < n; i++)
		if (a->buf[i] != b->buf[i])
			return ((int)(unsigned char)a->buf[i] -
			    (int)(unsigned char)b->buf[i]);
	return ((int)a->len - (int)b->len);
}

/* 中文：（ASCII 构建）把大写 ASCII 字符折叠为小写 */
/* EN: (ASCII build) Fold an uppercase ASCII character to lowercase */
static char
kstr_foldc(char c)
{

	if (c >= 'A' && c <= 'Z')
		return (char)(c - 'A' + 'a');
	return (c);
}

/* 中文：（ASCII 构建）忽略大小写比较两个 ASCII kstr */
/* EN: (ASCII build) Compare two ASCII kstrs case-insensitively */
int
kstr_icmp(const struct kstr *a, const struct kstr *b)
{
	unsigned i, n;
	char ca, cb;

	n = (a->len < b->len) ? a->len : b->len;
	for (i = 0; i < n; i++) {
		ca = kstr_foldc(a->buf[i]);
		cb = kstr_foldc(b->buf[i]);
		if (ca != cb)
			return ((int)(unsigned char)ca -
			    (int)(unsigned char)cb);
	}
	return ((int)a->len - (int)b->len);
}

/* 中文：（ASCII 构建）对折叠为小写的字符计算 kstr 的 FNV 哈希 */
/* EN: (ASCII build) Compute the FNV hash of a kstr over characters folded to lowercase */
unsigned
kstr_hash(const struct kstr *s)
{
	unsigned i, h = 2166136261u;

	for (i = 0; i < s->len; i++) {
		h ^= (unsigned char)kstr_foldc(s->buf[i]);
		h *= 16777619u;
	}
	return (h);
}

/* 中文：（ASCII 构建）判断 kstr 是否与给定 ASCII 字符串相等 */
/* EN: (ASCII build) Test whether a kstr equals a given ASCII string */
int
kstr_eq_ascii(const struct kstr *s, const char *ascii)
{
	unsigned i, n = strlen(ascii);

	if (s->len != n)
		return (0);
	for (i = 0; i < n; i++)
		if (s->buf[i] != ascii[i])
			return (0);
	return (1);
}

/* 中文：（ASCII 构建）复制 ASCII kstr，先检查目标容量是否够 */
/* EN: (ASCII build) Copy an ASCII kstr after checking that the destination has enough capacity */
int
kstr_copy(struct kstr *dst, const struct kstr *src)
{

	if (src->len + 1 > dst->maxlen)
		return (-1);
	bcopy(src->buf, dst->buf, src->len);
	dst->len = src->len;
	dst->buf[dst->len] = 0;
	return (0);
}

/* 中文：（ASCII 构建）把源 ASCII kstr 追加到目标的末尾 */
/* EN: (ASCII build) Append a source ASCII kstr to the end of a destination */
int
kstr_append(struct kstr *dst, const struct kstr *src)
{

	if (dst->len + src->len + 1 > dst->maxlen)
		return (-1);
	bcopy(src->buf, dst->buf + dst->len, src->len);
	dst->len += src->len;
	dst->buf[dst->len] = 0;
	return (0);
}

/* 中文：（ASCII 构建）把 ASCII 字符串追加到 kstr 的末尾 */
/* EN: (ASCII build) Append an ASCII string to the end of a kstr */
int
kstr_append_ascii(struct kstr *dst, const char *ascii)
{
	unsigned n = strlen(ascii);

	if (dst->len + n + 1 > dst->maxlen)
		return (-1);
	bcopy(ascii, dst->buf + dst->len, n);
	dst->len += n;
	dst->buf[dst->len] = 0;
	return (0);
}

/* 中文：（ASCII 构建）判断一个 kstr 是否是另一个的前缀（忽略大小写） */
/* EN: (ASCII build) Test whether one kstr is a prefix of another, ignoring case */
int
kstr_prefix(const struct kstr *prefix, const struct kstr *path)
{

	if (prefix->len > path->len)
		return (0);
	if (prefix->len == 0)
		return (1);
	return (kstr_icmp(prefix, path) == 0);
}

/* 中文：（ASCII 构建）返回 ASCII kstr 的字符个数 */
/* EN: (ASCII build) Return the character count of an ASCII kstr */
unsigned
kstr_units(const struct kstr *s)
{

	return (s->len);
}

/* 中文：（ASCII 构建）返回指定下标的字节，下标越界时返回 0 */
/* EN: (ASCII build) Return the byte at a given index, or 0 when out of range */
unsigned
kstr_unit(const struct kstr *s, unsigned i)
{

	if (i >= s->len)
		return (0);
	return ((unsigned char)s->buf[i]);
}

/* 中文：（ASCII 构建）把一个字节（取低 8 位）追加到 kstr 的末尾 */
/* EN: (ASCII build) Push one byte (low 8 bits) onto the end of a kstr */
int
kstr_push_unit(struct kstr *s, unsigned c)
{

	if (s->len + 2 > s->maxlen)
		return (-1);
	s->buf[s->len++] = (char)(c & 0xff);
	s->buf[s->len] = 0;
	return (0);
}

#endif /* KERNEL_UNICODE */


/*
 * strings.c - the few libkern primitives not already in locore.s.
 * (locore.s supplies bcopy, bzero and blkclr.)
 */

#include <sys/param.h>
#include <sys/types.h>

/* 中文：比较两个字符串的前 n 个字符，相等返回 0，否则返回首个不同字符的无符号差值 */
/* EN: Compare up to n characters of two strings, returning 0 if equal or the unsigned difference of the first differing characters */
int
strncmp(const char *s1, const char *s2, unsigned int n)
{

	while (n-- > 0) {
		if (*s1 != *s2)
			return ((unsigned char)*s1 - (unsigned char)*s2);
		if (*s1 == 0)
			break;
		s1++;
		s2++;
	}
	return (0);
}

/* 中文：把 n 字节数据从 src 复制到 dst（调用 bcopy），返回目标指针 dst */
/* EN: Copy n bytes from src to dst via bcopy and return the destination pointer dst */
void *
memcpy(void *dst, const void *src, unsigned int n)
{

	bcopy(src, dst, n);
	return (dst);
}

/* 中文：把 dst 的前 n 个字节填充为字符 c（取低 8 位），返回 dst */
/* EN: Fill the first n bytes of dst with the byte value c and return dst */
void *
memset(void *dst, int c, unsigned int n)
{
	unsigned char *p = dst;

	while (n-- > 0)
		*p++ = (unsigned char)c;
	return (dst);
}

/* 中文：把 n 字节数据从 src 移动到 dst（调用 bcopy），返回目标指针 dst */
/* EN: Move n bytes from src to dst via bcopy and return the destination pointer dst */
void *
memmove(void *dst, const void *src, unsigned int n)
{

	bcopy(src, dst, n);
	return (dst);
}

/* 中文：在字符串 s 中查找字符 c 的首次出现，找到返回其指针，否则返回空指针（c 为 0 时返回结尾指针） */
/* EN: Find the first occurrence of character c in string s, returning a pointer to it or NULL (return the terminating null pointer when c is 0) */
char *
strchr(const char *s, int c)
{
	char ch = (char)c;

	while (*s) {
		if (*s == ch)
			return ((char *)s);
		s++;
	}
	return (ch == 0) ? (char *)s : 0;
}
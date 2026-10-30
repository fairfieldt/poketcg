/* The few libc functions the engine needs in the freestanding WebAssembly build.
 * (clang may also emit calls to memcpy/memset/memmove for struct copies.) */
#include <stddef.h>
#include "include/string.h"
#include "include/stdlib.h"

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	while (n--)
		*d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else if (d > s) {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;
	while (n--)
		*d++ = (unsigned char)c;
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;
	for (; n; n--, x++, y++)
		if (*x != *y)
			return *x - *y;
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b || !*a)
			return (unsigned char)*a - (unsigned char)*b;
	}
	return 0;
}

char *strcpy(char *dst, const char *src)
{
	char *d = dst;
	while ((*d++ = *src++))
		;
	return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
	size_t i = 0;
	for (; i < n && src[i]; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = 0;
	return dst;
}

char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c)
			return (char *)s;
		if (!*s)
			return NULL;
	}
}

int abs(int v)
{
	return v < 0 ? -v : v;
}

_Noreturn void abort(void)
{
	__builtin_trap();
}

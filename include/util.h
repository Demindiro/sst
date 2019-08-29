#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static char *strclone(const char *text) {
	size_t l = strlen(text);
	char *m = malloc(l + 1);
	if (m == NULL)
		return m;
	memcpy(m, text, l + 1);
	return m;
}
static char *strnclone(const char *text, size_t max) {
	size_t l = strlen(text);
	l = l > max ? max : l;
	char *m = malloc(l + 1);
	if (m == NULL)
		return m;
	memcpy(m, text, l + 1);
	m[l] = 0;
	return m;
}
#pragma GCC diagnostic pop

#define streq(x,y) (strcmp(x,y) == 0)
#define strstart(x,y) (strncmp(x,y,strlen(y)) == 0)
#define isnum(c) ('0' <= c && c <= '9')

#define SWAP(type,x,y) do {	\
	type SWAP = x;		\
	x = y;			\
	y = SWAP;		\
} while (0)

#ifndef NDEBUG
# define ERROR(m, ...) fprintf(stderr, "ERROR: " m "\n", ##__VA_ARGS__)
# define DEBUG(m, ...) fprintf(stderr, "DEBUG: " m "\n", ##__VA_ARGS__)
#else
# define ERROR(m, ...) fprintf(stderr, m "\n", ##__VA_ARGS__)
# define DEBUG(m, ...) NULL
#endif

#define _STR(x) #x
#define STR(x) _STR(x)

#define EXIT(c) do {				\
	ERROR(STR(__LINE__) "@" __FILE__);	\
	exit(c);				\
} while (0)

#endif

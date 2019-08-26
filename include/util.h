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
#pragma GCC diagnostic pop

#define streq(x,y) (strcmp(x,y) == 0)
#define strstart(x,y) (strncmp(x,y,strlen(y)) == 0)
#define isnum(c) ('0' <= c && c <= '9')

#define ERROR(m, ...) fprintf(stderr, m "\n", ##__VA_ARGS__)

#endif

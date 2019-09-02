#ifndef UTIL_H
#define UTIL_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



#ifndef thread_local
# if __STDC_VERSION__ >= 201112 && !defined __STDC_NO_THREADS__
#  define thread_local _Thread_local
# elif defined _WIN32 && ( \
       defined _MSC_VER || \
       defined __ICL || \
       defined __DMC__ || \
       defined __BORLANDC__ )
#  define thread_local __declspec(thread)
/* note that ICC (linux) and Clang are covered by __GNUC__ */
# elif defined __GNUC__ || \
       defined __SUNPRO_C || \
       defined __xlC__
#  define thread_local __thread
# else
#  error "Cannot define thread_local"
# endif
#endif



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
static char *num2str(ssize_t n)
{
	char buf[22];
	snprintf(buf, sizeof buf, "%ld", n);
	return strclone(buf);
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


#define _STR(x) #x
#define STR(x) _STR(x)


#ifndef NDEBUG
# define ERROR(m, ...) fprintf(stderr, "ERROR: [%s:%u] " m "\n", __func__, __LINE__, ##__VA_ARGS__)
# define DEBUG(m, ...) fprintf(stderr, "DEBUG: " m "\n", ##__VA_ARGS__)
# define WARN(m, ...) fprintf(stderr, "WARN: " m "\n", ##__VA_ARGS__);
#else
# define ERROR(m, ...) fprintf(stderr, m "\n", ##__VA_ARGS__)
# define DEBUG(m, ...) NULL
# define WARN(m, ...) fprintf(stderr, m "\n", ##__VA_ARGS__);
#endif


#define EXIT(c, m, ...) do {		\
	ERROR(m, ##__VA_ARGS__);	\
	exit(c);			\
} while (0)


#define STRERRNO strerror(errno)

#define EXITERRNO(m, c) do {							\
	if (c == 3)								\
		ERROR("[" STR(__LINE__) "@" __FILE__ "] " m ": %s", STRERRNO);	\
	else									\
		ERROR(m ": %s", STRERRNO);					\
	exit(c);								\
} while (0)


#endif

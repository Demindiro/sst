#ifndef LINES_H
#define LINES_H

#include <stddef.h>
#include <string.h>
#include "util.h"


typedef struct pos {
	size_t c;
	int x, y;
} pos_t;

typedef struct line {
	const char *text;
	pos_t pos;
} line_t;

#define DEBUGLINE(l) DEBUG("%4u:%-2u | %s", l.pos.y, l.pos.x, l.text);
#define PRINTLINE(l) ERROR("%4u:%-2u | %s", l.pos.y, l.pos.x, l.text);
#define PRINTLINEX(l,x,text) _print_line_pointer(text, l, (int)x);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void _print_line_pointer(const char *text, line_t l, int c)
{
	text += l.pos.c - l.pos.x + 1;
	char  buf[1024];
	char *end = strchr(text, '\n');
	size_t s = end - text;
	memcpy(buf, text, s < sizeof buf ? s : sizeof buf);
	for (size_t i = 0; i < s; i++)
		buf[i] = buf[i] == '\t' ? ' ' : buf[i];
	buf[s] = 0;
	ERROR("%4u:%-2u | %s", l.pos.y, l.pos.x + c, buf);
	// Yes, I hate it
#ifndef NDEBUG
	fprintf(stderr, "ERROR: ");
#endif
	fprintf(stderr, "        | ");
	for (size_t i = 0; i < l.pos.x + c - 1; i++)
		fprintf(stderr, "~");
	fprintf(stderr, "^\n");
}
#pragma GCC diagnostic pop

#endif

#ifndef LINES_H
#define LINES_H

#include <stddef.h>
#include "util.h"

typedef struct line {
	char  *text;
	size_t row;
} line_t;

#define PRINTLINE(l) ERROR("%4lu | %s", l.row, l.text)

#endif

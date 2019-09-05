#include <stdio.h>
#include "lines.h"
#include "func.h"
#include "hashtbl.h"



int parsefunc_header(struct func *f, const line_t line, const char *text);


void lines2func(const line_t *lines, size_t linecount,
                func f, hashtbl functbl,
		const char *text);

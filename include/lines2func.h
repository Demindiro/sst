#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "lines.h"
#include "func.h"
#include "hashtbl.h"
#include "util.h"


int parsefunc_header(struct func *f, const line_t line, const char *text);


int lines2func(const line_t *lines, size_t linecount,
               struct func *f, struct hashtbl *functbl);

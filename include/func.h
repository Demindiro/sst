#ifndef FUNC_H
#define FUNC_H

#include "util.h"

#define FUNC_LINE_NONE    0
#define FUNC_LINE_ASSIGN  1
#define FUNC_LINE_DECLARE 2
#define FUNC_LINE_DESTROY 3
#define FUNC_LINE_FUNC    4
#define FUNC_LINE_GOTO    5
#define FUNC_LINE_IF      6
#define FUNC_LINE_LABEL   7
#define FUNC_LINE_LOAD    8
#define FUNC_LINE_MATH    9
#define FUNC_LINE_RETURN 10
#define FUNC_LINE_STORE  11

#define MATH_ADD    VASM_OP_ADD
#define MATH_SUB    VASM_OP_SUB
#define MATH_MUL    VASM_OP_MUL
#define MATH_DIV    VASM_OP_DIV
#define MATH_MOD    VASM_OP_MOD
#define MATH_NOT    VASM_OP_NOT
#define MATH_INV    VASM_OP_INV
#define MATH_RSHIFT VASM_OP_RSHIFT
#define MATH_LSHIFT VASM_OP_LSHIFT
#define MATH_AND    VASM_OP_AND
#define MATH_OR     VASM_OP_OR
#define MATH_XOR    VASM_OP_XOR
#define MATH_LOADAT (-43) // IDK man
#define MATH_LESS   VASM_OP_LESS
#define MATH_LESSE  VASM_OP_LESSE


struct func_line {
	char type;
};

struct func_line_assign {
	struct func_line line;
	const char *var;
	const char *value;
	char cons;
};

struct func_line_declare {
	struct func_line line;
	const char *type;
	const char *var;
};

#define func_line_destroy func_line_declare

struct func_line_func {
	struct func_line line;
	unsigned char argcount;
	const char  *name;
	const char **args;
	const char  *var;
};

struct func_line_goto {
	struct func_line line;
	const char *label;
};

struct func_line_if {
	struct func_line line;
	const char *label;
	const char *var;
	char inv;
};

struct func_line_label {
	struct func_line line;
	const char *label;
};

struct func_line_math {
	struct func_line line;
	char op;
	const char *x, *y, *z;
};

struct func_line_return {
	struct func_line line;
	const char *val;
};

struct func_line_store {
	struct func_line line;
	const char *var;
	const char *val;
	const char *index;
};

union func_line_all_p {
	struct func_line *line;
	struct func_line_assign  *a;
	struct func_line_declare *d;
	struct func_line_func    *f;
	struct func_line_goto    *g;
	struct func_line_if      *i;
	struct func_line_label   *l;
	struct func_line_math    *m;
	struct func_line_return  *r;
	struct func_line_store   *s;
};


struct func_arg {
	const char *type;
	const char *name;
};


typedef struct func {
	const char *type;
	const char *name;
	unsigned char argcount;
	unsigned char linecount;
	struct func_arg   args[16];
	struct func_line *lines[256];
} func_t, *func;


// TODO move definitions to .c file

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

/*****
 * Shorthand functions for common lines
 ***/
static void _insert_line(func f, size_t *k, struct func_line *l)
{
	f->lines[*k] = l;
	(*k)++;
}


static void line_declare(func f, size_t *k, const char *name, const char *type)
{
	struct func_line_declare *d = malloc(sizeof *d);
	d->line.type = FUNC_LINE_DECLARE;
	d->var       = name;
	d->type      = type;
	_insert_line(f, k, (struct func_line *)d);
}


static void line_math(func f, size_t *k, int op, const char *x, const char *y, const char *z)
{
	struct func_line_math *m = malloc(sizeof *m);
	m->line.type = FUNC_LINE_MATH;
	m->op        = op;
	m->x         = x;
	m->y         = y;
	m->z         = z;
	_insert_line(f, k, (struct func_line *)m);
}


static void line_destroy(func f, size_t *k, const char *var)
{
	struct func_line_destroy *d = malloc(sizeof *d);
	d->line.type = FUNC_LINE_DESTROY;
	d->var       = var;
	_insert_line(f, k, (struct func_line *)d);
}


static void line_function(func f, size_t *k, const char *var, const char *func,
                          size_t argcount, const char **args)
{
	struct func_line_func *g = malloc(sizeof *g);
	g->line.type = FUNC_LINE_FUNC;
	g->var       = var;
	g->name      = func;
	g->argcount  = argcount;
	g->args      = malloc(argcount * sizeof *g->args);
	for (size_t i = 0; i < argcount; i++)
		g->args[i] = args[i];
	_insert_line(f, k, (struct func_line *)g);
}


static const char *_new_temp_var(func f, size_t *k, const char *type, const char *name)
{
	static int i = 0;
	char b[256];
	if (name != NULL)
		snprintf(b, sizeof b, "_%s_%u", name, i++);
	else
		snprintf(b, sizeof b, "_temp_%u", i++);
	char *v = strclone(b);
	line_declare(f, k, v, type);
	return v;
}

#pragma GCC diagnostic pop

#endif

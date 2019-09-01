#include <assert.h>
#include "func.h"
#include "util.h"

/*****
 * Shorthand functions for common lines
 ***/

void insert_line(func f, struct func_line *l)
{
	if (f->linecount >= f->linecap) {
		size_t s = f->linecap * 3 / 2;
		struct func_line **t = realloc(f->lines, s * sizeof *t);
		if (t == NULL)
			EXITERRNO("Failed to reallocate lines array", 3);
		f->lines   = t;
		f->linecap = s;
	}
	assert(f->linecount < f->linecap);
	f->lines[f->linecount++] = l;
}


void line_assign(func f, const char *var, const char *val)
{
	struct func_line_assign *a = malloc(sizeof *a);
	a->type  = ASSIGN;
	a->var   = var;
	a->value = val;
	a->cons  = 0;
	insert_line(f, (struct func_line *)a);
}


void line_declare(func f, const char *name, const char *type)
{
	struct func_line_declare *d = malloc(sizeof *d);
	d->_type = DECLARE;
	d->var   = name;
	d->type  = type;
	insert_line(f, (struct func_line *)d);
}


void line_destroy(func f, const char *var)
{
	struct func_line_destroy *d = malloc(sizeof *d);
	d->_type = DESTROY;
	d->var   = var;
	insert_line(f, (struct func_line *)d);
}


void line_function(func f, const char *var, const char *func,
                   size_t argcount, const char **args)
{
	struct func_line_func *g = malloc(sizeof *g);
	g->type     = FUNC;
	g->var      = var;
	g->name     = func;
	g->argcount = argcount;
	g->args     = malloc(argcount * sizeof *g->args);
	for (size_t i = 0; i < argcount; i++)
		g->args[i] = args[i];
	insert_line(f, (struct func_line *)g);
}


void line_goto(func f, const char *label)
{
	struct func_line_goto *g = malloc(sizeof *g);
	g->type  = GOTO;
	g->label = label;
	insert_line(f, (struct func_line *)g);
}


void line_if(func f, const char *val, const char *label)
{
	struct func_line_if *i = malloc(sizeof *i);
	i->type  = IF;
	i->label = label;
	i->var   = val;
	i->inv   = 0;
	insert_line(f, (struct func_line *)i);	
}


void line_label(func f, const char *label)
{
	struct func_line_label *l = malloc(sizeof *l);
	l->type  = LABEL;
	l->label = label;
	insert_line(f, (struct func_line *)l);
}


void line_math(func f, int op, const char *x, const char *y, const char *z)
{
	struct func_line_math *m = malloc(sizeof *m);
	m->type = MATH;
	m->op   = op;
	m->x    = x;
	m->y    = y;
	m->z    = z;
	insert_line(f, (struct func_line *)m);
}


void line_return(func f, const char *val)
{
	struct func_line_return *r = malloc(sizeof *r);
	r->type = RETURN;
	r->val  = val;
	insert_line(f, (struct func_line *)r);
}


void line_store(func f, const char *arr, const char *index, const char *val)
{
	struct func_line_store *s = malloc(sizeof *s);
	s->type  = STORE;
	s->var   = arr;
	s->index = index;
	s->val   = val;
	insert_line(f, (struct func_line *)s);
}


const char *new_temp_var(func f, const char *type, const char *name)
{
	static int i = 0;
	char b[256];
	if (name != NULL)
		snprintf(b, sizeof b, "_%s_%u", name, i++);
	else
		snprintf(b, sizeof b, "_temp_%u", i++);
	char *v = strclone(b);
	line_declare(f, v, type);
	return v;
}


struct func_line *copy_line(const struct func_line *l)
{
	union func_line_all_p a = { .line = (struct func_line *)l };
	size_t s;
	switch (l->type) {
	case ASSIGN : s = sizeof *a.a; break;
	case DECLARE: s = sizeof *a.d; break;
	case DESTROY: s = sizeof *a.d; break;
	case GOTO   : s = sizeof *a.g; break;
	case IF     : s = sizeof *a.i; break;
	case LOAD   : s = sizeof *a.l; break;
	case MATH   : s = sizeof *a.m; break;
	case FUNC   : s = sizeof *a.f; break;
	case RETURN : s = sizeof *a.r; break;
	case STORE  : s = sizeof *a.s; break;
	default:
		ERROR("Unknown line type (%d)", l->type);
		EXIT(1);
	}
	a.a = malloc(s);
	memcpy(a.a, l, s);
	return a.line;
}

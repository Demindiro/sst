#include "func.h"

/*****
 * Shorthand functions for common lines
 ***/

void insert_line(func f, struct func_line *l)
{
	size_t k = f->linecount;
	f->lines[k] = l;
	f->linecount++;
}


void line_assign(func f, const char *var, const char *val)
{
	struct func_line_assign *a = malloc(sizeof *a);
	a->line.type = FUNC_LINE_ASSIGN;
	a->var       = var;
	a->value     = val;
	insert_line(f, (struct func_line *)a);
}


void line_declare(func f, const char *name, const char *type)
{
	struct func_line_declare *d = malloc(sizeof *d);
	d->line.type = FUNC_LINE_DECLARE;
	d->var       = name;
	d->type      = type;
	insert_line(f, (struct func_line *)d);
}


void line_destroy(func f, const char *var)
{
	struct func_line_destroy *d = malloc(sizeof *d);
	d->line.type = FUNC_LINE_DESTROY;
	d->var       = var;
	insert_line(f, (struct func_line *)d);
}


void line_function(func f, const char *var, const char *func,
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
	insert_line(f, (struct func_line *)g);
}


void line_goto(func f, const char *label)
{
	struct func_line_goto *g = malloc(sizeof *g);
	g->line.type = FUNC_LINE_GOTO;
	g->label     = label;
	insert_line(f, (struct func_line *)g);
}


void line_if(func f, const char *val, const char *label)
{
	struct func_line_if *i = malloc(sizeof *i);
	i->line.type = FUNC_LINE_IF;
	i->label     = label;
	i->var       = val;
	insert_line(f, (struct func_line *)i);	
}


void line_label(func f, const char *label)
{
	struct func_line_label *l = malloc(sizeof *l);
	l->line.type = FUNC_LINE_LABEL;
	l->label     = label;
	insert_line(f, (struct func_line *)l);
}


void line_math(func f, int op, const char *x, const char *y, const char *z)
{
	struct func_line_math *m = malloc(sizeof *m);
	m->line.type = FUNC_LINE_MATH;
	m->op        = op;
	m->x         = x;
	m->y         = y;
	m->z         = z;
	insert_line(f, (struct func_line *)m);
}


void line_return(func f, const char *val)
{
	struct func_line_return *r = malloc(sizeof *r);
	r->line.type = FUNC_LINE_RETURN;
	r->val       = val;
	insert_line(f, (struct func_line *)r);
}


void line_store(func f, const char *arr, const char *index, const char *val)
{
	struct func_line_store *s = malloc(sizeof *s);
	s->line.type = FUNC_LINE_STORE;
	s->var       = arr;
	s->index     = index;
	s->val       = val;
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

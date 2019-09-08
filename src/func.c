#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "expr.h"
#include "func.h"
#include "hashtbl.h"
#include "types.h"
#include "util.h"

#ifndef NDEBUG
thread_local func _current_func;
#endif
struct hashtbl functions;

/*****
 * Shorthand functions for common lines
 ***/

void insert_line(func f, struct func_line *l)
{
	if (f->linecount >= f->linecap) {
		size_t s = f->linecap * 3 / 2;
		struct func_line **t = realloc(f->lines, s * sizeof *t);
		if (t == NULL)
			EXITERRNO(3, "Failed to reallocate lines array");
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


void line_asm(func f, const char **vasms, size_t vasmcount,
              const char **invars , const char *inregs , size_t incount,
              const char **outvars, const char *outregs, size_t outcount)
{
	struct func_line_asm *a = malloc(sizeof *a);
	a->type      = ASM;
	a->vasms     = vasms;
	a->vasmcount = vasmcount;
	a->incount   = incount;
	a->outcount  = outcount;
	for (size_t i = 0; i < incount; i++) {
		a->invars[i] = invars[i];
		a->inregs[i] = inregs[i];
	}
	for (size_t i = 0; i < outcount; i++) {
		a->outvars[i] = outvars[i];
		a->outregs[i] = outregs[i];
	}
	insert_line(f, (struct func_line *)a);
}


void line_declare(func f, const char *name, const char *type, hashtbl variables)
{
	assert(type != NULL);
	struct func_line_declare *d = malloc(sizeof *d);
	d->_type = DECLARE;
	d->var   = name;
	d->type  = type;
	insert_line(f, (struct func_line *)d);
	h_add(variables, name, (size_t)type);
}


void line_destroy(func f, const char *var, hashtbl variables)
{
	struct func_line_destroy *d = malloc(sizeof *d);
	d->_type = DESTROY;
	d->var   = var;
	insert_line(f, (struct func_line *)d);
	h_rem(variables, var);
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


void line_function_parse(func f, const char *var, const char *str,
                         hashtbl variables)
{
	const char *c = str;
	while (*c != ' ' && *c != 0)
		c++;
	const char *name = strnclone(str, c - str);
	func g = get_function(name, variables);
	if (g == NULL)
		EXIT(1, "Function '%s' not declared", name);
	size_t      argcount = 0;
	const char *args [32];
	char        etemp[32];
	if (*c != 0) {
		c++;
		while (1) {
			str = c;
			while (*c != ',' && *c != 0)
				c++;
			char b[2048];
			memcpy(b, str, c - str);
			b[c - str] = 0;
			const char *type = g->args[argcount].type;
			args[argcount] = parse_expr(f, b, &etemp[argcount], type, variables);
			if (!etemp[argcount])
				args[argcount] = strclone(args[argcount]);
			argcount++;
			if (*c == 0)
				break;
			c++;
		}
	}
	const char **a = args;
	line_function(f, var, g->name, argcount, a);
	for (size_t i = argcount - 1; i != -1; i--) {
		if (etemp[i])
			line_destroy(f, args[i], variables);
	}
}


void line_goto(func f, const char *label)
{
	struct func_line_goto *g = malloc(sizeof *g);
	g->type  = GOTO;
	g->label = label;
	insert_line(f, (struct func_line *)g);
}


void line_if(func f, const char *val, const char *label, int inv)
{
	struct func_line_if *i = malloc(sizeof *i);
	i->type  = IF;
	i->label = label;
	i->var   = val;
	i->inv   = inv;
	insert_line(f, (struct func_line *)i);	
}


void line_label(func f, const char *label)
{
	struct func_line_label *l = malloc(sizeof *l);
	l->type  = LABEL;
	l->label = label;
	insert_line(f, (struct func_line *)l);
}


void line_load(func f, const char *var, const char *pointer, const char *index)
{
	line_math(f, MATH_LOADAT, var, pointer, index);
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

void line_throw(func f, const char *expr)
{
	struct func_line *l = malloc(sizeof *l);
	l->type = THROW;
	insert_line(f, l);
}


const char *new_temp_var(func f, const char *type, const char *name, hashtbl variables)
{
	static int i = 0;
	const char *v;
	if (name != NULL)
		v = strprintf("__%s%u", name, i++);
	else
		v = strprintf("__t%u", i++);
	line_declare(f, v, type, variables);
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
		EXIT(1, "Unknown line type (%d)", l->type);
	}
	a.a = malloc(s);
	memcpy(a.a, l, s);
	return a.line;
}



static const char *mathop2str(int op)
{
	switch (op) {
	case MATH_ADD:    return "+";
	case MATH_SUB:    return "-";
	case MATH_MUL:    return "*";
	case MATH_DIV:    return "/";
	case MATH_REM:    return "%";
	case MATH_MOD:    return "%%";
	//case MATH_NOT:    return "~";
	//case MATH_INV:    return "!";
	case MATH_RSHIFT: return ">>";
	case MATH_LSHIFT: return "<<";
	case MATH_XOR:    return "^";
	case MATH_LESS:   return "<";
	case MATH_LESSE:  return "<=";
	case MATH_L_AND:  return "&&";
	case MATH_L_OR:   return "||";
	default: EXIT(1, "Invalid op (%d)\n", op);
	}
}




void line2str(struct func_line *l, char *buf, size_t bufsize)
{
	union func_line_all_p fl = { .line = l } ;
	size_t n;
	switch (l->type) {
	case ASSIGN:
		n = snprintf(buf, bufsize, "ASSIGN     %s = %s", fl.a->var, fl.a->value);
		if (fl.a->cons)
			snprintf(buf + n, bufsize - n, "  # const");
		break;
	case ASM:
		n = snprintf(buf, bufsize, "ASM        IN ");
		for (size_t i = 0; i < fl.as->incount; i++)
			n += snprintf(buf + n, bufsize - n, " r%d = %s,",
					fl.as->inregs[i], fl.as->invars[i]);
		buf[n-1] = '\n';
		n += snprintf(buf + n, bufsize - n, "                   OUT");
		for (size_t i = 0; i < fl.as->outcount; i++)
			n += snprintf(buf + n, bufsize - n, " %s = r%d,",
					fl.as->outvars[i], fl.as->outregs[i]);
		buf[n-1] = '\n';
		for (size_t i = 0; i < fl.as->vasmcount; i++)
			n += snprintf(buf + n, bufsize - n, "                     %s\n",
					fl.as->vasms[i]);
		buf[n-1] = 0;
		break;
	case DECLARE:
		snprintf(buf, bufsize, "DECLARE    %s %s", fl.d->type, fl.d->var);
		break;
	case DESTROY:
		snprintf(buf, bufsize, "DESTROY    %s", fl.d->var);
		break;
	case FUNC:
		n = snprintf(buf, bufsize, "FUNCTION  ");
		if (fl.f->var != NULL)
			n += snprintf(buf + n, bufsize - n, " %s =", fl.f->var);
		n += snprintf(buf + n, bufsize - n, " %s", fl.f->name);
		if (fl.f->argcount > 0)
			n += snprintf(buf + n, bufsize - n, " %s", fl.f->args[0]);
		for (size_t k = 1; k < fl.f->argcount; k++)
			n += snprintf(buf + n, bufsize - n, ",%s", fl.f->args[k]);
		break;
	case GOTO:
		snprintf(buf, bufsize, "GOTO       %s", fl.g->label);
		break;
	case IF:
		snprintf(buf, bufsize, "IF         %s%s THEN %s",
		         fl.i->inv ? "NOT " : "", fl.i->var, fl.i->label);
		break;
	case LABEL:
		snprintf(buf, bufsize, "LABEL      %s", fl.l->label);
		break;
	case MATH:
		if (fl.m->op == MATH_INV)
			snprintf(buf, bufsize, "MATH       %s = !%s", fl.m->x, fl.m->y);
		else if (fl.m->op == MATH_NOT)
			snprintf(buf, bufsize, "MATH       %s = ~%s", fl.m->x, fl.m->y);
		else if (fl.m->op == MATH_LOADAT)
			snprintf(buf, bufsize, "MATH       %s = *(%s + %s)", fl.m->x, fl.m->y, fl.m->z);
		else
			snprintf(buf, bufsize, "MATH       %s = %s %s %s", fl.m->x, fl.m->y, mathop2str(fl.m->op), fl.m->z);
		break;
	case RETURN:
		snprintf(buf, bufsize, "RETURN     %s", fl.r->val);
		break;
	case STORE:
		snprintf(buf, bufsize, "STORE      *(%s + %s) = %s", fl.s->var, fl.s->index, fl.s->val);
		break;
	case THROW:
		snprintf(buf, bufsize, "THROW");
		break;
	default:
		EXIT(1, "Unknown line type (%d)", l->type);
	}
#undef LDEBUG
}



int add_function(func f)
{
	if (functions.len == 0)
		h_create(&functions, 32);
	return h_add(&functions, f->name, (size_t)f);
}



func get_function(const char *str, hashtbl variables)
{
	const char *name = get_function_name(str, variables);
	if (name == NULL)
		name = str;
	func f;
	if (h_get2(&functions, name, (size_t *)&f) < 0)
		return NULL;
	return f;
}

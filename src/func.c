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
	default: fprintf(stderr, "Invalid op (%d)\n", op); abort();
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
			snprintf(buf, bufsize, "MATH       %s = %s[%s]", fl.m->x, fl.m->y, fl.m->z);
		else
			snprintf(buf, bufsize, "MATH       %s = %s %s %s", fl.m->x, fl.m->y, mathop2str(fl.m->op), fl.m->z);
		break;
	case RETURN:
		snprintf(buf, bufsize, "RETURN     %s", fl.r->val);
		break;
	case STORE:
		snprintf(buf, bufsize, "STORE      %s[%s] = %s", fl.s->var, fl.s->index, fl.s->val);
		break;
	default:
		ERROR("Unknown line type (%d)", l->type);
		EXIT(1);
	}
#undef LDEBUG
}

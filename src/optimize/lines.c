#include "optimize/lines.h"
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "hashtbl.h"
#include "lines.h"
#include "util.h"


enum optimize_lines_options optimize_lines_options;


#define REMOVEAT(x) do {					\
	f->linecount--;						\
	memmove(f->lines + (x), f->lines + (x) + 1,		\
	        (f->linecount - (x)) * sizeof f->lines[(x)]);	\
} while (0)

// x inclusive, y exclusive
#define REMOVERANGE(x, y) do {					\
	assert((y) >= (x));					\
	memmove(f->lines + (x), f->lines + (y),			\
	        (f->linecount - (y)) * sizeof f->lines[(x)]);	\
	f->linecount -= (y) - (x);				\
} while (0)




/**
 * Remove assignments where the value of the result is unused or
 * overwritten before it is used.
 * This also works for math and function assignments.
 */
static int _remove_unused_assign(struct func *f, size_t *i, struct hashtbl *h_const)
{
	const char *v;
	union func_line_all_p l = { .line = f->lines[*i] };
	int isfunc;

	// Determine how to extract needed variable based on line type
	switch (l.line->type) {
	case FUNC_LINE_ASSIGN: v = l.a->var; isfunc = 0; break;
	case FUNC_LINE_FUNC:   v = l.f->var; isfunc = 1; break;
	case FUNC_LINE_MATH:   v = l.m->x  ; isfunc = 0; break;
	}
	// Can't remove a variable that doesn't exist :P
	if (v == NULL)
		return 0;
	for (size_t k = *i + 1; k < f->linecount; k++) {
		l.line = f->lines[k];
		switch (l.line->type) {
		case FUNC_LINE_ASSIGN:
			if (streq(v, l.a->value))
				goto used;
			if (streq(v, l.a->var))
				goto notused;
			break;
		case FUNC_LINE_GOTO:
		case FUNC_LINE_IF:
			// Assume used for now
			// To actually determine usage we have to follow the labels
			// while also (partially) solving the halting problem.
			goto used;
		case FUNC_LINE_FUNC:
			for (size_t j = 0; j < l.f->argcount; j++) {
				if (streq(v, l.f->args[j]))
					goto used;
			}
			break;
		case FUNC_LINE_MATH:
			if (streq(v, l.m->y) ||
			    (l.m->z != NULL && streq(v, l.m->z)))
				goto used;
			if (streq(v, l.m->x))
				goto notused;
			break;
		case FUNC_LINE_RETURN:
			if (streq(v, l.r->val))
				goto used;
			break;
		case FUNC_LINE_STORE:
			if (streq(v, l.s->var) ||
			    streq(v, l.s->val) ||
			    streq(v, l.s->index))
				goto used;
			break;
		}
	}
notused:
	if (isfunc) {
		l.line   = f->lines[*i];
		l.f->var = NULL;
	} else {
		REMOVEAT(*i);
	}
	return 1;
used:
	return 0;
}


/**
 * TODO: Labels & goto statements should also be checked
 * Move a destroy statement as much up as possible without altering program
 * behaviour to potentially free up registers and make other optimizations
 * feasible.
 */
static int _early_destroy(struct func *f, size_t *i)
{
	// Find the last occurence of the variable
	size_t lastk  = *i;
	struct func_line_declare *dl = (struct func_line_declare *)f->lines[*i];
	union  func_line_all_p    fl;
	const char *v = dl->var;
	for (size_t k = lastk + 1; k < f->linecount; k++) {
		fl.line = f->lines[k];
		switch(fl.line->type) {
		case FUNC_LINE_ASSIGN:
			if (streq(v, fl.a->value) || streq(v, fl.a->var))
				lastk = k;
			break;
		case FUNC_LINE_DECLARE:
			if (streq(v, fl.d->var)) {
				// This shouldn't happen before a destroy statement is encountered
				ERROR("Double declare for variable '%s'", v);
				abort();
			}
			break;
		case FUNC_LINE_DESTROY:
			if (streq(v, fl.d->var)) {
				// Can the destroy statement be moved up?
				if (lastk + 1 != k) {
					REMOVEAT(k);
					goto move_destroy;
				} else {
					goto already_destroyed;
				}
			}
			break;
		case FUNC_LINE_FUNC:
			if (fl.f->var != NULL && streq(v, fl.f->var)) {
				lastk = k;
			} else {
				for (size_t p = 0; p < fl.f->argcount; p++) {
					if (streq(v, fl.f->args[p])) {
						lastk = k;
						break;
					}
				}
			}
			break;
		case FUNC_LINE_IF:
			if (streq(v, fl.i->var))
				lastk = k;
			break;
		case FUNC_LINE_MATH:
			if (streq(v, fl.m->x) || streq(v, fl.m->y) ||
			    (fl.m->z != NULL && streq(v, fl.m->z))) {
				lastk = k;
			}
			break;
		}
	}
	// No destroy line found, so create one
	fl.line       = calloc(sizeof *fl.d, 1);
	fl.line->type = FUNC_LINE_DESTROY;
	fl.d->var     = (char *)v;
move_destroy:
	memmove(f->lines + lastk + 2, f->lines + lastk + 1,
		(f->linecount - lastk - 1) * sizeof *f->lines);
	f->linecount++;
	f->lines[lastk + 1] = fl.line;
	return 1;
already_destroyed:
	return 0;
}


/**
 * Replace if statements that use constant values with goto statements or remove
 * them altogether.
 */
static int _constant_if(struct func *f, size_t *i, struct hashtbl *h_const)
{
	size_t j;
	union func_line_all_p fl = { .line = f->lines[*i] };
	if (isnum(*fl.i->var) || h_get2(h_const, fl.i->var, &j) != -1) {
		if (isnum(*fl.i->var))
			j = atoi(fl.i->var);
		if ((j == 0) == fl.i->inv) {
			const char *lbl = fl.i->label;
			struct func_line_goto *g;
			g            = calloc(sizeof *fl.g, 1);
			g->line.type = FUNC_LINE_GOTO;
			g->label     = lbl;
			f->lines[*i] = (struct func_line *)g;
		} else {
			REMOVEAT(*i);
		}
		return 1;
	}
	return 0;
}


/**
 * Replace (usually) slow 'mod' and 'div' instructions with faster 'and' or
 * 'rshift' instructions if feasible.
 */
static int _fast_div(struct func *f, size_t *i)
{
	union func_line_all_p fl = { .line = f->lines[*i] };
	if (fl.m->z != NULL && isnum(*fl.m->z)) {
		if (fl.m->op == MATH_DIV) {
			size_t n = 0, l;
			size_t z = strtol(fl.m->z, NULL, 0);
			for (size_t k = 0; k < 64; k++) {
				if (z & 1) {
					n++;
					l = k;
				}
				z >>= 1;
			}
			if (n == 1) {
				fl.m->op = MATH_RSHIFT;
				char b[16];
				snprintf(b, sizeof b, "%lu", l);
				fl.m->z  = strclone(b);
				return 1;
			}
		} else if (fl.m->op == MATH_MOD) {
			size_t n = 0;
			size_t z = strtol(fl.m->z, NULL, 0), oz = z;
			for (size_t k = 0; k < 64; k++) {
				if (z & 1)
					n++;
				z >>= 1;
			}
			if (n == 1) {
				fl.m->op = MATH_AND;
				char b[16];
				snprintf(b, sizeof b, "0x%lx", oz - 1);
				fl.m->z  = strclone(b);
				return 1;
			}
		}
	}
	return 0;
}


/**
 * Replace math statements that are effectively the same as assignments
 * with 'assign' statements.
 */
static int _nop_math(struct func *f, size_t *i)
{
	struct func_line_math *l = (struct func_line_math *)f->lines[*i];
	if (l->op == MATH_ADD) {
		if (!streq(l->y, "0") && !streq(l->z, "0"))
			return 0;
	} else if (l->op == MATH_SUB) {
		if (!streq(l->z, "0"))
			return 0;
	} else if (l->op == MATH_MUL) {
		if (!streq(l->y, "0") && !streq(l->z, "0"))
			return 0;
	} else if (l->op == MATH_DIV || l->op == MATH_MOD) {
		if (!streq(l->z, "1"))
			return 0;
	} else {
		return 0;
	}
	const char *var = l->x,
	           *val = streq(l->y, "0") ? l->z : l->y;
	struct func_line_assign *a;
	a            = calloc(sizeof *a, 1);
	a->line.type = FUNC_LINE_ASSIGN;
	a->var       = var;
	a->value     = val;
	f->lines[*i]  = (struct func_line *)a;
	return 1;
}


/**
 * Check if a variable can be substituted for another.
 * e.g.
 * - DECLARE x
 * - ASSIGN  x = y
 * - DESTROY y
 * In this case x can as well be replaced by y since it effectively takes over it's role
 * To prevent name clashes in case of a second declare a RENAME statement is used, which
 * changes the 'name' of a register in func2vasm.
 */
static int _rename_var(struct func *f, size_t *i)
{
	if (*i >= f->linecount - 2)
		return 0;
	union func_line_all_p fl0 = { .line = f->lines[*i + 0] },
	                      fl1 = { .line = f->lines[*i + 1] },
	                      fl2 = { .line = f->lines[*i + 2] };
	if (fl1.line->type == FUNC_LINE_ASSIGN  &&
	    fl2.line->type == FUNC_LINE_DESTROY) {
		if (streq(fl0.d->var, fl1.a->var) &&
		    streq(fl1.a->value, fl2.d->var)) {
			struct func_line_rename *r = malloc(sizeof *r);
			r->line.type = FUNC_LINE_RENAME;
			r->old       = fl2.d->var;
			r->new       = fl0.d->var;
			f->lines[*i] = (struct func_line *)r;
			REMOVERANGE(*i + 1, *i + 3);
			return 1;
		}
	}
	return 0;
}


/**
 * Check if a math inversion before an if can be replaced by a reversed if.
 * This only applies if the variable is destroyed before the inversion has
 * side effects.
 */
static int _inverse_math_if(struct func *f, size_t *i)
{
	if (*i >= f->linecount - 2)
		return 0;
	union func_line_all_p fl0 = { .line = f->lines[*i + 0] },
	                      fl1 = { .line = f->lines[*i + 1] },
	                      fl2 = { .line = f->lines[*i + 2] };
	if (fl0.m->op == MATH_INV &&
	    fl1.line->type == FUNC_LINE_IF &&
	    fl2.line->type == FUNC_LINE_DESTROY) {
		if (streq(fl0.m->x, fl1.i->var) && streq(fl1.i->var, fl2.d->var)) {
			fl1.i->inv = !fl1.i->inv;
			REMOVEAT(*i);
			return 1;
		}
	}
	return 0;
}


/**
 * If statements often have temporary variables that are simply assigned the
 * value of another variable only to be destroyed immediatly after the if
 * statement.
 * In such cases the temporary variable can be removed and the assignee used
 * directly.
 */
static int _substitute_temp_var(struct func *f, size_t *i)
{
	if (*i >= f->linecount - 3)
		return 0;
	union func_line_all_p fl0 = { .line = f->lines[*i + 0] },
	                      fl1 = { .line = f->lines[*i + 1] },
	                      fl2 = { .line = f->lines[*i + 2] },
	                      fl3 = { .line = f->lines[*i + 3] };
	if (fl3.line->type == FUNC_LINE_DESTROY &&
	    streq(fl0.d->var, fl3.d->var)) {
		if (fl1.line->type == FUNC_LINE_ASSIGN &&
		    fl2.line->type == FUNC_LINE_IF     &&
		    streq(fl0.d->var, fl1.a->var)      &&
		    streq(fl0.d->var, fl2.i->var)) {
			fl2.i->var = fl1.a->value;
			f->lines[*i] = fl2.line;
		} else if (fl1.line->type == FUNC_LINE_MATH   &&
		           fl2.line->type == FUNC_LINE_ASSIGN &&
		           streq(fl0.d->var, fl1.m->x)        &&
			   streq(fl0.d->var, fl2.a->value)) {
			fl1.m->x = fl2.a->var;
			f->lines[*i] = fl1.line;
		} else {
			return 0;
		}
		REMOVERANGE(*i + 1, *i + 4);
		return 1;
	}
	return 0;
}


/**
 * Remove redundant inversions by simply inverting the if statement
 */
static int _invert_if(struct func *f, size_t *i)
{
	struct func_line_math *m = (struct func_line_math *)f->lines[*i];
	if (m->op == MATH_INV && streq(m->x, m->y)) {
		for (size_t j = *i + 1; j < f->linecount - 1; j++) {
			union func_line_all_p l0 = { .line = f->lines[j + 0] },
					      l1 = { .line = f->lines[j + 1] };
			if (l0.line->type == FUNC_LINE_IF &&
			    l1.line->type == FUNC_LINE_DESTROY &&
			    streq(m->x, l0.i->var) && streq(m->x, l1.d->var)) {
				l0.i->inv = !l0.i->inv;
				REMOVEAT(*i);
				return 1;
			}
		}
	}
	return 0;
}


/**
 * Remove unused (private) labels
 * Labels prevent some optimizations, hence removing them is still potentially useful
 */
static int _remove_unused_label(func f, size_t *i)
{
	const char *lbl = ((struct func_line_label *)f->lines[*i])->label;
	for (size_t j = 0; j < f->linecount; j++) {
		union func_line_all_p l = { .line = f->lines[j] };
		if (l.line->type == FUNC_LINE_GOTO) {
			if (streq(lbl, l.g->label))
				return 0;
		} else if (l.line->type == FUNC_LINE_IF) {
			if (streq(lbl, l.i->label))
				return 0;
		}
	}
	REMOVEAT(*i);
	return 0;
}


#if 0
// IDK what this does tbh
static void _findconst(struct func *f)
{
	struct hashtbl h;
	h_create(&h, 16);
	for (size_t i = 0; i < f->linecount; i++) {
		union func_line_all_p l = { .line = f->lines[i] };
		size_t j;
		switch (l.line->type) {
		case FUNC_LINE_DECLARE:
			h_add(&h, l.d->var, 0);
			break;
		case FUNC_LINE_ASSIGN:
			j = h_get(&h, l.a->var);
			if (j == -1) {
				// Is a function. Skip
			} else if (j == 0) {
				// Not assigned yet, so potentially constant
				h_rem(&h, l.d->var);
				h_add(&h, l.d->var, i);
				l.a->cons = 1;
			} else {
				// Reassigned, so not constant
				((struct func_line_assign *)f->lines[j])->cons = 0;
				h_rem(&h, l.a->var);
			}
			break;
		case FUNC_LINE_MATH:
			j = h_get(&h, l.m->x);
			if (j != -1 && j != 0) {
				// Reassigned, so not constant
				((struct func_line_assign *)f->lines[j])->cons = 0;
				h_rem(&h, l.m->x);
			}
			break;
		}
	}
	for (size_t i = 0; i < f->linecount; i++) {
		union func_line_all_p l = { .line = f->lines[i] };
		if (l.line->type == FUNC_LINE_DECLARE || l.line->type == FUNC_LINE_DESTROY) {
			size_t j = h_get(&h, l.d->var);
			if (j != -1 && j != 0) {
				REMOVEAT(*i);
			}
		}
	}
}
#else
#define _findconst(f) NULL
#endif



void optimizefunc(struct func *f)
{
	struct hashtbl h_const;
	h_create(&h_const, 4);
	// Looping works well enough
	for (size_t _ = 0; _ < 5; _++) {
		if (optimize_lines_options & FINDCONST)
			_findconst(f);
		for (size_t i = 0; i < f->linecount; i++) {
			union func_line_all_p fl = { .line = f->lines[i] };
			switch (f->lines[i]->type) {
			case FUNC_LINE_ASSIGN:
				// Determine if the value is a constant
				// This is necessary for other optimizations
				if (fl.a->cons && isnum(*fl.a->value)) {
					size_t j = strtol(fl.a->value, NULL, 0);
					h_add(&h_const, fl.a->var, j);
				}
				if (optimize_lines_options & REMOVE_UNUSED_ASSIGN)
					// Break if any change occured
					if (_remove_unused_assign(f, &i, &h_const))
						break;
				break;
			case FUNC_LINE_DECLARE:
				if (optimize_lines_options & SUBSTITUTE_TEMP_IF_VAR)
					if (_substitute_temp_var(f, &i))
						break;
				if (optimize_lines_options & RENAME_VAR)
					if (_rename_var(f, &i))
						break;
				// Totally broken, do not use
				// This optimization does not take in account gotos
				if (0 && optimize_lines_options & EARLY_DESTROY)
					if (_early_destroy(f, &i))
						break;
				break;
			case FUNC_LINE_MATH:
				if (optimize_lines_options & REMOVE_UNUSED_ASSIGN)
					if (_remove_unused_assign(f, &i, &h_const))
						break;
				if (optimize_lines_options & INVERSE_MATH_IF)
					if (_inverse_math_if(f, &i))
						break;
				if (optimize_lines_options & FAST_DIV)
					if (_fast_div(f, &i))
						break;
				if (optimize_lines_options & NOP_MATH)
					if (_nop_math(f, &i))
						break;
				if (optimize_lines_options & INVERT_IF)
					if (_invert_if(f, &i))
						break;
				break;
			case FUNC_LINE_FUNC:
				if (optimize_lines_options & REMOVE_UNUSED_ASSIGN)
					if (_remove_unused_assign(f, &i, &h_const))
						break;
				break;
			case FUNC_LINE_IF:
				if (optimize_lines_options & CONSTANT_IF)
					if (_constant_if(f, &i, &h_const))
						break;
				break;
			case FUNC_LINE_LABEL:
				if (optimize_lines_options & REMOVE_UNUSED_LABEL)
					if (_remove_unused_label(f, &i))
						break;
				break;
			}
		}
	}
}

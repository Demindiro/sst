#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "func.h"
#include "hashtbl.h"
#include "util.h"



#define ENOTDECLARED(x) do { 			\
	ERROR("Variable '%s' not declared", x);	\
	EXIT(1);				\
} while (0)


static size_t _get_type_size(const char *type)
{
	if (streq(type, "long" ) || streq(type, "ulong" ))
		return 8;
	if (streq(type, "int"  ) || streq(type, "uint"  ))
		return 4;
	if (streq(type, "short") || streq(type, "ushort"))
		return 2;
	if (streq(type, "byte" ) || streq(type, "ubyte" ))
		return 1;
	ERROR("Unknown size for type '%s'", type);
	EXIT(1);
}



static void _reserve_stack_space(union vasm_all **v, size_t *vc, char reg, const char *type)
{
	const char *c = strchr(type, '[');
	if (c != NULL) {
		c++;
		const char *d = strchr(c, ']');
		char b[64];
		if (c == d) {
			// Dynamic array
			return;
		} else {
			// Fixed array
			memcpy(b, c, d - c);
			b[d - c] = 0;
			size_t l = atol(b);
			memcpy(b, type, (c - 1) - type);
			b[(c - 1) - type] = 0;
			l *= _get_type_size(b);
			DEBUG("%lu bytes allocated on stack", l);
			snprintf(b, sizeof b, "%lu", l);

			union vasm_all a;

			a.rs.op = OP_SET;
			a.rs.r  = 29;
			a.rs.str= strclone(b);
			(*v)[(*vc)++] = a;

			a.r2.op = OP_MOV;
			a.r2.r[0]=reg;
			a.r2.r[1]=31;
			(*v)[(*vc)++] = a;

			a.r3.op = OP_ADD;
			a.r3.r[0]=31;
			a.r3.r[1]=31;
			a.r3.r[2]=29;
			(*v)[(*vc)++] = a;
		}
	}
}



// TODO: yuck
static const char *_get_var_type(func f, ssize_t k, const char *var)
{
	for ( ; k >= 0; k--) {
		union func_line_all_p l = { .line = f->lines[k] };
		if (l.line->type == DECLARE && streq(var, l.d->var))
			return l.d->type;
	}
	ENOTDECLARED(var);
}



int func2vasm(union vasm_all **vasms, size_t *vasmcount, struct func *f) {
	size_t vc = 0, vs = 1024;
	union vasm_all *v = malloc(vs * sizeof *v);
	union vasm_all a;
	union func_line_all_p l;

	*vasms = malloc(1024 * sizeof **vasms);

	a.s.op  = OP_LABEL;
	a.s.str = f->name;
	v[vc++] = a;

	struct hashtbl tbl;
	if (h_create(&tbl, 8) < 0) {
		perror("Failed to create hash table");
		return -1;
	}

	char allocated_regs[32];
	char is_const_reg_zero[32];
	memset(allocated_regs, 0, sizeof allocated_regs);
	memset(is_const_reg_zero, 0, sizeof is_const_reg_zero);

	// Add function arguments
	for (size_t i = 0; i < f->argcount; i++) {
		h_add(&tbl, f->args[i].name, i);
		allocated_regs[i] = 1;
	}

	// Add constants (if not too many)
	size_t consts[8];
	size_t constcount = 0;
	for (size_t i = f->argcount; i < f->linecount && constcount < sizeof consts / sizeof *consts; i++) {
		l.line = f->lines[i];
		switch (l.line->type) {
		case ASSIGN:
			if (l.a->cons)
				consts[constcount++] = i;
			break;
		case MATH:
			if (isnum(*l.m->y))
				consts[constcount++] = i;
			if (l.m->z != NULL && isnum(*l.m->z))
				consts[constcount++] = i;
			break;
		default:
			break;
		}
	}

	if (constcount >= sizeof consts / sizeof *consts)
		constcount = 0;

	// Preserve stack pointer
	a.r.op = OP_PUSH;
	a.r.r  = 30;
	v[vc++] = a;
	a.r2.op = OP_MOV;
	a.r2.r[0]= 30;
	a.r2.r[1]= 31;
	v[vc++] = a;

	struct hashtbl constvalh;
	h_create(&constvalh, 16);
	for (size_t i = f->argcount; i < constcount; i++) {
		l.line   = f->lines[consts[i]];
		a.op     = OP_SET;
		a.rs.r   = i;
		const char *key, *val;
		char b[64];
		static size_t bc = 0;
		char use_y;
		switch (l.line->type) {
		case ASSIGN:
			key = l.a->var;
			val = l.a->value;
			break;
		case MATH:
			snprintf(b, sizeof b, "_const_%lu", bc);
			bc++;
			key = strclone(b);
			if (isnum(*l.m->y)) {
				use_y = 1;
				val = l.m->y;
				l.m->y = key;
			} else {
				use_y = 0;
				val = l.m->z;
				l.m->z = key;
			}
			break;
		default:
			break;
		}
		const char *okey = NULL;
		if (h_get2(&constvalh, val, (size_t *)&okey) != -1) {
			switch (l.line->type) {
			case ASSIGN:
				break;
			case MATH:
				if (use_y)
					l.m->y = okey;
				else
					l.m->z = okey;
				break;
			default:
				break;
			}
		} else {
			a.rs.str = val;
			h_add(&tbl, key, i);
			h_add(&constvalh, val, (size_t)key);
			allocated_regs[i] = 1;
			is_const_reg_zero[i] =
				!(isnum(*val) && strtol(val, NULL, 0) == 0);
			v[vc++] = a;
		}
	}

	for (size_t i = 0; i < f->linecount; i++) {
		union  func_line_all_p   fl = { .line = f->lines[i] };
		struct func_line_func   *flf;
		struct func_line_goto   *flg;
		struct func_line_if     *fli;
		struct func_line_label  *fll;
		struct func_line_math   *flm;
		size_t ra, rb, reg;
		switch (f->lines[i]->type) {
		case ASSIGN:
			if (fl.a->cons && h_get(&tbl, fl.a->var) != -1)
				break;
			reg = h_get(&tbl, fl.a->var);
			if (reg == -1)
				ENOTDECLARED(fl.a->var);
			if ('0' <= *fl.a->var && *fl.a->var <= '9') {
				ERROR("You can't assign to a number");
				EXIT(1);
			}
			if ('0' <= *fl.a->value && *fl.a->value <= '9') {
				a.rs.op  = OP_SET;
				a.rs.r   = reg;
				a.rs.str = fl.a->value;
			} else {
				a.r2.op   = OP_MOV;
				a.r2.r[0] = reg;
				reg = h_get(&tbl, fl.a->value);
				if (reg == -1)
					ENOTDECLARED(fl.a->value);
				a.r2.r[1] = reg;
			}
			v[vc++] = a;
			break;
		case DECLARE:
			for (reg = 0; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
				if (!allocated_regs[reg]) {
					allocated_regs[reg] = 1;
					break;
				}
			}
			if (h_add(&tbl, fl.d->var, reg) < 0) {
				ERROR("Failed to add variable to hashtable");
				EXIT(1);
			}
			if (fl.d->type == NULL) {
				ERROR("=== !!! ===  Bug: DECLARE type is NULL for variable '%s'", fl.d->var);
				ERROR("=== !!! ===  Assuming and setting type to 'long'");
				ERROR("=== !!! ===  This will be a fatal error in the future");
				fl.d->type = "long";
				fl.d->type = "byte";
			}
			_reserve_stack_space(&v, &vc, reg, fl.d->type);
			break;
		case DESTROY:
			// TODO
			if (fl.d->var[1] == '.')
				break;
			reg = h_get(&tbl, fl.d->var);
			if (reg == -1)
				ENOTDECLARED(fl.d->var);
			allocated_regs[reg] = 0;
			h_rem(&tbl, fl.d->var);
			break;
		case FUNC:
			flf = (struct func_line_func *)f->lines[i];

			// Push registers that are in use
			for (size_t j = 0; j < 32; j++) {
				if (allocated_regs[j]) {
					a.r.op = OP_PUSH;
					a.r.r  = j;
					v[vc++] = a;
				}
			}

			// Push the needed arguments
			for (ssize_t j = flf->argcount - 1; j >= 0; j--) {
				int r = h_get(&tbl, flf->args[j]);
				if (r != -1 && r != j) {
					a.r.op  = OP_PUSH;
					a.r.r   = r;
					v[vc++] = a;
				}
			}

			// Pop or set the arguments
			for (size_t j = 0; j < flf->argcount; j++) {
				int r = h_get(&tbl, flf->args[j]);
				if (r == -1) {
					a.rs.op  = OP_SET;
					a.rs.r   = j;
					a.rs.str = flf->args[j];
					v[vc++] = a;
				} else if (r != j) {
					a.r.op = OP_POP;
					a.r.r  = j;
					v[vc++] = a;
				}
			}

			// Call
			a.s.op  = OP_CALL;
			a.s.str = flf->name;
			v[vc++] = a;

			// Pop registers
			for (int j = 31; j >= (flf->var != NULL ? 1 : 0); j--) {
				if (allocated_regs[j]) {
					a.r.op  = OP_POP;
					a.r.r   = j;
					v[vc++] = a;
				}
			}

			// Check if function assigns to var
			if (flf->var != NULL) {
				size_t r;
				if (h_get2(&tbl, flf->var, &r) < 0)
					ENOTDECLARED(flf->var);
				// Move the returned value to the variable
				a.r2.op  = OP_MOV;
				a.r2.r[0]= r;
				a.r2.r[1]= 0; 
				v[vc++]  = a;
				// Pop remaining registers
				if (allocated_regs[0]) {
					a.r.op  = OP_POP;
					a.r.r   = 0;
					v[vc++] = a;
				}
			}
			break;
		case GOTO:
			flg = (struct func_line_goto *)f->lines[i];
			a.s.op  = OP_JMP;
			a.s.str = flg->label;
			v[vc++] = a;
			break;
		case IF:
			fli = (struct func_line_if *)f->lines[i];
			if (isnum(*fli->var)) {
				a.rs.op  = OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = fli->var;
				v[vc++] = a;
			} else {
				ra = h_get(&tbl, fli->var);
				if (ra == -1)
					ENOTDECLARED(fl.i->var);
			}
			rb = 0;
			a.r2s.op   = fli->inv ? OP_JZ : OP_JNZ;
			a.r2s.r[0] = ra;
			a.r2s.r[1] = rb;
			a.r2s.str  = fli->label;
			v[vc++] = a;
			break;
		case LABEL:
			fll = (struct func_line_label *)f->lines[i];
			a.s.op  = OP_LABEL;
			a.s.str = fll->label;
			v[vc++] = a;
			break;
		case MATH:
			flm = (struct func_line_math *)f->lines[i];
			if (isnum(*flm->x)) {
				ERROR("You can't assign to a number");
				EXIT(1);
			}
			if (isnum(*flm->y)) {
				a.rs.op  = OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = flm->y;
				v[vc++] = a;
			} else {
				ra = h_get(&tbl, flm->y);
				if (ra == -1)
					ENOTDECLARED(flm->y);
			}
			if (flm->op != MATH_INV && flm->op != MATH_NOT) {
				if (isnum(*flm->z)) {
					a.rs.op  = OP_SET;
					a.rs.r   = rb = 21;
					a.rs.str = flm->z;
					v[vc++] = a;
				} else {
					// TODO
					rb = h_get(&tbl, flm->z);
					if (rb == -1)
						ENOTDECLARED(flm->z);
				}
				if (flm->op == MATH_LOADAT) {
					const char *t = _get_var_type(f, i, flm->x);
					switch(_get_type_size(t)) {
					case 1: a.r3.op = OP_LDBAT; break;
					case 2: a.r3.op = OP_LDSAT; break;
					case 4: a.r3.op = OP_LDIAT; break;
					case 8: a.r3.op = OP_LDLAT; break;
					default:
						ERROR("Dunno");
						EXIT(1);
					}
				} else {
					a.r3.op = flm->op;
				}
				a.r3.r[0] = h_get(&tbl, flm->x);
				a.r3.r[1] = ra;
				a.r3.r[2] = rb;
				if (a.r3.r[0] == -1) {
					for ( ; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
						if (!allocated_regs[reg]) {
							allocated_regs[reg] = 1;
							break;
						}
					}
					a.r3.r[0] = reg;
					h_add(&tbl, flm->x, reg);
				}
			} else {
				if (h_get2(&tbl, flm->x, &reg) < 0)
					ENOTDECLARED(flm->x);
				a.r2.op   = flm->op;
				a.r2.r[0] = h_get(&tbl, flm->x);
				a.r2.r[1] = ra;
				if (a.r2.r[0] == -1) {
					size_t reg = 0;
					for ( ; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
						if (!allocated_regs[reg]) {
							allocated_regs[reg] = 1;
							break;
						}
					}
					a.r2.r[0] = reg;
					h_add(&tbl, flm->x, reg);
				}
			}
			v[vc++] = a;
			break;
		case RETURN:
			// Restore stack pointer
			a.r2.op = OP_MOV;
			a.r2.r[0]= 31;
			a.r2.r[1]= 30;
			v[vc++] = a;
			a.r.op = OP_POP;
			a.r.r  = 30;
			v[vc++] = a;
			if (isnum(*fl.r->val)) {
				a.rs.op  = OP_SET;
				a.rs.r   = 0;
				a.rs.str = fl.r->val;
			} else {
				a.r2.op  = OP_MOV;
				a.r2.r[0]= 0;
				a.r2.r[1]= h_get(&tbl, fl.r->val);
				if (a.r2.r[1] == -1)
					ENOTDECLARED(fl.r->val);
			}
			v[vc++] = a;
			v[vc++].op = OP_RET;
			break;
		case STORE:
			if (isnum(*fl.s->var)) {
				ERROR("You can't index a number");
				EXIT(1);
			}
			if (isnum(*fl.s->val)) {
				a.rs.op  = OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = fl.s->val;
				v[vc++] = a;
			} else {
				ra = h_get(&tbl, fl.s->val);
				if (ra == -1)
					ENOTDECLARED(fl.s->val);
			}
			if (isnum(*fl.s->index)) {
				a.rs.op  = OP_SET;
				a.rs.r   = rb = 21;
				a.rs.str = fl.s->val;
				v[vc++] = a;
			} else {
				rb = h_get(&tbl, fl.s->index);
				if (rb == -1)
					ENOTDECLARED(fl.s->index);
			}
			a.r3.op = OP_STRBAT; // TODO determine required length
			a.r3.r[0] = ra;
			a.r3.r[1] = h_get(&tbl, fl.s->var);
			a.r3.r[2] = rb;
			if (a.r2.r[1] == -1)
				ENOTDECLARED(fl.s->var);
			v[vc++] = a;
			break;
		default:
			ERROR("Unknown line type (%d)", f->lines[i]->type);
			EXIT(1);
		}
	}
	// Restore stack pointer
	a.r2.op = OP_MOV;
	a.r2.r[0]= 31;
	a.r2.r[1]= 30;
	v[vc++] = a;
	a.r.op = OP_POP;
	a.r.r  = 30;
	v[vc++] = a;
	v[vc++].op = OP_RET;
	*vasms     = realloc(v, vc * sizeof *v);
	*vasmcount = vc;
	return 0;
}

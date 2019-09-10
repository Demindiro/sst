#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "func.h"
#include "hashtbl.h"
#include "util.h"
#include "text2vasm.h"
#include "types.h"



#define ENOTDECLARED(x) EXIT(1, "Variable '%s' not declared", x);


static size_t _get_type_size(const char *type)
{
	struct type t;
	if (get_type(&t, type) < 0)
		EXIT(3, "Unknown type '%s'", type);
	switch (t.type) {
	case TYPE_NUMBER:
		; struct type_meta_number *mn = (void *)&t.meta;
		return mn->size;
	case TYPE_POINTER:
	case TYPE_ARRAY:
	case TYPE_CLASS:
		return 8;
	case TYPE_STRUCT:
		EXIT(4, "");
	}
	EXIT(1, "Unknown size for type '%s'", type);
}



static void _reserve_stack_space(union vasm_all **v, size_t *vc, char reg, const char *type)
{
	const char *c = strchr(type, '[');
	if (c != NULL) {
		c++;
		const char *d = strchr(c, ']');
		if (c == d) {
			// Dynamic array
			return;
		} else {
			// Fixed array
			char b[21];
			memcpy(b, c, d - c);
			b[d - c] = 0;
			size_t l = atol(b);
			memcpy(b, type, (c - 1) - type);
			b[(c - 1) - type] = 0;
			l *= _get_type_size(b);
			DEBUG("%lu bytes allocated on stack", l);

			union vasm_all a;

			a.rs.op = OP_SET;
			a.rs.r  = 29;
			a.rs.s  = strprintf("%lu", l);
			(*v)[(*vc)++] = a;

			a.r2.op = OP_MOV;
			a.r2.r0=reg;
			a.r2.r1=31;
			(*v)[(*vc)++] = a;

			a.r3.op = OP_ADD;
			a.r3.r0=31;
			a.r3.r1=31;
			a.r3.r2=29;
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
	if (streq(f->name, "main"))
		a.s.s = "main";
	else
		a.s.s = strprintf("%s_%u", f->name, f->argcount);
	v[vc++] = a;

	struct hashtbl tbl;
	if (h_create(&tbl, 8) < 0) {
		perror("Failed to create hash table");
		return -1;
	}

	char allocated_regs[32];
	const char *regs_types[32];
	char is_const_reg_zero[32];
	memset(allocated_regs, 0, sizeof allocated_regs);
	memset(is_const_reg_zero, 0, sizeof is_const_reg_zero);

	struct hashtbl structs;
	h_create(&structs, 1);

	// Add function arguments
	for (size_t i = 0, r = 0; i < f->argcount; i++) {
		; struct type type;
		const char *ft = f->args[i].type, *fn = f->args[i].name;
		if (get_type(&type, ft) < 0)
			EXIT(3, "Type '%s' not declared", ft);
		if (type.type == TYPE_STRUCT) {
			if (h_add(&structs, fn, (size_t)ft) < 0)
				EXIT(3, "Failed to add variable to hashtable");
			struct type_meta_struct *m = (void *)&type.meta;
			for (size_t j = 0; j < m->count; j++) {
				const char *n = strprintf("%s@%s", fn, m->names[j]);
				if (h_add(&tbl, n, r) < 0)
					EXIT(3, "Failed to add variable to hashtable");
				allocated_regs[r++] = 1;
			}
		} else {
			if (h_add(&tbl, fn, r) < 0)
				EXIT(3, "Failed to add variable to hashtable");
			allocated_regs[r++] = 1;
		}
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
	a.r2.r0= 30;
	a.r2.r1= 31;
	v[vc++] = a;

	struct hashtbl constvalh;
	h_create(&constvalh, 16);
	for (size_t i = f->argcount; i < constcount; i++) {
		l.line   = f->lines[consts[i]];
		a.op     = OP_SET;
		a.rs.r   = i;
		const char *key, *val;
		static size_t bc = 0;
		char use_y;
		switch (l.line->type) {
		case ASSIGN:
			key = l.a->var;
			val = l.a->value;
			break;
		case MATH:
			key = strprintf("__const_%lu", bc);
			bc++;
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
			a.rs.s = val;
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
			if (isnum(*fl.a->var))
				EXIT(1, "You can't assign to a number");
			if (isnum(*fl.a->value)) {
				a.rs.op = OP_SET;
				a.rs.r  = reg;
				a.rs.s  = fl.a->value;
			} else {
				a.r2.op = OP_MOV;
				a.r2.r0 = reg;
				a.r2.r1 = h_get(&tbl, fl.a->value);
				if (a.r2.r1 == -1) {
					//ENOTDECLARED(fl.a->value);
					a.rs.op = OP_SET;
					a.rs.r  = reg;
					a.rs.s  = fl.a->value;
				}
			}
			v[vc++] = a;
			break;
		case ASM:
			// Preserve register contents
			for (size_t i = 0; i < 32; i++) {
				if (allocated_regs[i]) {
					a.r.op  = OP_PUSH;
					a.r.r   = i;
					v[vc++] = a;
				}
			}
			// In arguments
			for (size_t i = fl.as->incount - 1; i != -1; i--) {
				if (h_get2(&tbl, fl.as->invars[i], &reg) == -1)
					ENOTDECLARED(fl.as->invars[i]);
				a.r.op  = OP_PUSH;
				a.r.r   = reg;
				v[vc++] = a;
			}
			for (size_t i = 0; i < fl.as->incount; i++) {
				a.r.op  = OP_POP;
				a.r.r   = fl.as->inregs[i];
				v[vc++] = a;
			}
			// Insert assembly (TODO)
			for (size_t i = 0; i < fl.as->vasmcount; i++) {
				char buf[32], *b = buf;
				const char *c = fl.as->vasms[i];
				while (*c != ' ' && *c != 0)
					*b++ = *c++;
				*b = 0;
				if (*c != 0)
					c++;
				a.op = getop(buf);
				b = buf;
				while (*c != 0)
					*b++ = *c++;
				*b = 0;
				parse_op_args(&a, buf);
				v[vc++] = a;
			}
			// Out arguments
			for (size_t i = 0; i < fl.as->outcount; i++) {
				a.r.op  = OP_PUSH;
				a.r.r   = fl.as->outregs[i];
				v[vc++] = a;
			}
			for (size_t i = fl.as->outcount - 1; i != -1; i--) {
				if (h_get2(&tbl, fl.as->outvars[i], &reg) == -1)
					ENOTDECLARED(fl.as->outvars[i]);
				a.r.op  = OP_POP;
				a.r.r   = reg;
				v[vc++] = a;
			}
			// Pop register contents
			for (size_t i = 31; i != -1; i--) {
				if (allocated_regs[i]) {
					a.r.op  = OP_POP;
					a.r.r   = i;
					v[vc++] = a;
				}
			}
			break;
		case DECLARE:
			; struct type type;
			if (get_type(&type, fl.d->type) < 0)
				EXIT(3, "Type '%s' not declared", fl.d->type);
			if (type.type == TYPE_STRUCT) {
				if (h_add(&structs, fl.d->var, (size_t)fl.d->type) < 0)
					EXIT(3, "Failed to add variable to hashtable");
				struct type_meta_struct *m = (void *)&type.meta;
				for (size_t i = 0; i < m->count; i++) {
					for (reg = 0; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
						if (!allocated_regs[reg]) {
							allocated_regs[reg] = 1;
							break;
						}
					}
					const char *n = strprintf("%s@%s", fl.d->var, m->names[i]);
					if (h_add(&tbl, n, reg) < 0)
						EXIT(3, "Failed to add variable to hashtable");
					regs_types[reg] = fl.d->type;
				}
			} else {
				for (reg = 0; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
					if (!allocated_regs[reg]) {
						allocated_regs[reg] = 1;
						break;
					}
				}
				if (h_add(&tbl, fl.d->var, reg) < 0)
					EXIT(3, "Failed to add variable to hashtable");
				regs_types[reg] = fl.d->type;
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
			for (size_t j = flf->argcount - 1; j != -1; j--) {
				int r = h_get(&tbl, flf->args[j]);
				if (r != -1 && r != j) {
					a.r.op  = OP_PUSH;
					a.r.r   = r;
					v[vc++] = a;
				}
			}

			// Pop or set the arguments
			for (size_t j = 0; j < flf->argcount; j++) {
				size_t r = h_get(&tbl, flf->args[j]);
				if (r == -1) {
					// Get the struct's members
					const char *type;
					if (h_get2(&structs, flf->args[j], (size_t *)&type) < 0) {
						a.rs.op  = OP_SET;
						a.rs.r   = j;
						a.rs.s = flf->args[j];
						v[vc++] = a;
					} else {
						struct type t;
						get_type(&t, type);
						struct type_meta_struct *m = (void *)&t.meta;
						// Move the returned values to the variable
						for (size_t i = 0; i < m->count; i++) {
							char b[256];
							snprintf(b, sizeof b, "%s@%s", flf->var, m->names[i]);
							if (h_get2(&tbl, b, &r) < 0)
								ENOTDECLARED(b);
							a.r2.op = OP_MOV;
							a.r2.r0 = r;
							a.r2.r1 = i;
							v[vc++] = a;
						}
					}
				} else if (r != j) {
					a.r.op = OP_POP;
					a.r.r  = j;
					v[vc++] = a;
				}
			}

			// Call
			a.s.op  = OP_CALL;
			if (streq(flf->name, "main"))
				a.s.s = "main";
			else
				a.s.s = strprintf("%s_%u", flf->name, flf->argcount);
			v[vc++] = a;

			char arg_regs[32] = {};

			// Check if function assigns to var
			if (flf->var != NULL) {
				size_t r;
				if (h_get2(&tbl, flf->var, &r) < 0) {
					// Get the struct's members
					const char *type;
					if (h_get2(&structs, flf->var, (size_t *)&type) < 0)
						ENOTDECLARED(flf->var);
					struct type t;
					get_type(&t, type);
					struct type_meta_struct *m = (void *)&t.meta;
					// Move the returned values to the variable
					for (size_t i = 0; i < m->count; i++) {
						char b[256];
						snprintf(b, sizeof b, "%s@%s", flf->var, m->names[i]);
						if (h_get2(&tbl, b, &r) < 0)
							ENOTDECLARED(b);
						a.r2.op = OP_MOV;
						a.r2.r0 = r;
						a.r2.r1 = i;
						v[vc++] = a;
						arg_regs[r] = 1;
					}
				} else {
					// Move the returned value to the variable
					a.r2.op = OP_MOV;
					a.r2.r0 = r;
					a.r2.r1 = 0; 
					v[vc++] = a;
					arg_regs[r] = 1;
				}
			}

			// Pop registers
			for (size_t j = 31; j != -1; j--) {
				if (allocated_regs[j]) {
					if (arg_regs[j]) {
						a.rs.op = OP_SET;
						a.rs.r  = 29;
						a.rs.s  = "8";
						v[vc++] = a;
						a.r3.op = OP_SUB;
						a.r3.r0 = 31;
						a.r3.r1 = 31;
						a.r3.r2 = 29;
						v[vc++] = a;
					} else {
						a.r.op  = OP_POP;
						a.r.r   = j;
						v[vc++] = a;
					}
				}
			}

			break;
		case GOTO:
			flg = (struct func_line_goto *)f->lines[i];
			a.s.op  = OP_JMP;
			a.s.s = flg->label;
			v[vc++] = a;
			break;
		case IF:
			fli = (struct func_line_if *)f->lines[i];
			if (isnum(*fli->var)) {
				a.rs.op  = OP_SET;
				a.rs.r   = ra = 20;
				a.rs.s = fli->var;
				v[vc++] = a;
			} else {
				ra = h_get(&tbl, fli->var);
				if (ra == -1)
					ENOTDECLARED(fl.i->var);
			}
			a.rs.op = fli->inv ? OP_JZ : OP_JNZ;
			a.rs.r  = ra;
			a.rs.s  = fli->label;
			v[vc++] = a;
			break;
		case LABEL:
			fll = (struct func_line_label *)f->lines[i];
			a.s.op  = OP_LABEL;
			a.s.s = fll->label;
			v[vc++] = a;
			break;
		case MATH:
			flm = (struct func_line_math *)f->lines[i];
			if (isnum(*flm->x))
				EXIT(1, "You can't assign to a number");
			if (isnum(*flm->y)) {
				a.rs.op  = OP_SET;
				a.rs.r   = ra = 20;
				a.rs.s = flm->y;
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
					a.rs.s = flm->z;
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
						EXIT(1, "Dunno");
					}
				} else {
					a.r3.op = flm->op;
				}
				a.r3.r0 = h_get(&tbl, flm->x);
				a.r3.r1 = ra;
				a.r3.r2 = rb;
				if (a.r3.r0 == -1) {
					for ( ; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
						if (!allocated_regs[reg]) {
							allocated_regs[reg] = 1;
							break;
						}
					}
					a.r3.r0 = reg;
					h_add(&tbl, flm->x, reg);
				}
			} else {
				if (h_get2(&tbl, flm->x, &reg) < 0)
					ENOTDECLARED(flm->x);
				a.r2.op   = flm->op;
				a.r2.r0 = h_get(&tbl, flm->x);
				a.r2.r1 = ra;
				if (a.r2.r0 == -1) {
					size_t reg = 0;
					for ( ; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
						if (!allocated_regs[reg]) {
							allocated_regs[reg] = 1;
							break;
						}
					}
					a.r2.r0 = reg;
					h_add(&tbl, flm->x, reg);
				}
			}
			v[vc++] = a;
			break;
		case RETURN:
			// Restore stack pointer
			a.r2.op = OP_MOV;
			a.r2.r0 = 31;
			a.r2.r1 = 30;
			v[vc++] = a;
			a.r.op  = OP_POP;
			a.r.r   = 30;
			v[vc++] = a;
			if (isnum(*fl.r->val)) {
				a.rs.op = OP_SET;
				a.rs.r  = 0;
				a.rs.s  = fl.r->val;
				v[vc++] = a;
			} else {
				a.r2.op = OP_MOV;
				a.r2.r0 = 0;
				a.r2.r1 = h_get(&tbl, fl.r->val);
				if (a.r2.r1 == -1) {
					// Get the struct's members
					const char *type;
					if (h_get2(&structs, fl.r->val, (size_t *)&type) < 0) {
						a.rs.op = OP_SET;
						a.rs.r  = 0;
						a.rs.s  = fl.r->val;
						v[vc++] = a;
					} else {
						size_t r;
						struct type t;
						get_type(&t, type);
						struct type_meta_struct *m = (void *)&t.meta;
						// Push the struct's variables
						for (size_t i = 0; i < m->count; i++) {
							char b[256];
							snprintf(b, sizeof b, "%s@%s", fl.r->val, m->names[i]);
							if (h_get2(&tbl, b, &r) < 0)
								ENOTDECLARED(b);
							a.r.op  = OP_PUSH;
							a.r.r   = r;
							v[vc++] = a;
						}
						// Pop the variables
						for (size_t i = m->count - 1; i != -1; i--) {
							a.r.op  = OP_POP;
							a.r.r   = i;
							v[vc++] = a;
						}
					}
				} else {
					v[vc++] = a;
				}
			}
			v[vc++].op = OP_RET;
			break;
		case STORE:
			if (isnum(*fl.s->var))
				EXIT(1, "You can't index a number");
			if (isnum(*fl.s->val)) {
				a.rs.op  = OP_SET;
				a.rs.r   = ra = 20;
				a.rs.s = fl.s->val;
				v[vc++] = a;
			} else {
				ra = h_get(&tbl, fl.s->val);
				if (ra == -1)
					ENOTDECLARED(fl.s->val);
			}
			if (isnum(*fl.s->index)) {
				a.rs.op  = OP_SET;
				a.rs.r   = rb = 21;
				a.rs.s = fl.s->index;
				v[vc++] = a;
			} else {
				rb = h_get(&tbl, fl.s->index);
				if (rb == -1)
					ENOTDECLARED(fl.s->index);
			}
			switch(_get_type_size(regs_types[ra])) {
			case 1: a.r3.op = OP_STRBAT; break;
			case 2: a.r3.op = OP_STRSAT; break;
			case 4: a.r3.op = OP_STRIAT; break;
			case 8: a.r3.op = OP_STRLAT; break;
			default: EXIT(4, "TODO: all kinds of store stuff");
			}
			a.r3.r0 = ra;
			a.r3.r1 = h_get(&tbl, fl.s->var);
			a.r3.r2 = rb;
			if (a.r2.r1 == -1)
				ENOTDECLARED(fl.s->var);
			v[vc++] = a;
			break;
		case THROW:
			a.rs.op = OP_SET;
			a.rs.r  = 1;
			a.rs.s  = "9"; // abort
			v[vc++] = a;
			a.rs.op = OP_SET;
			a.rs.r  = 0;
			a.rs.s  = "9"; // signal
			v[vc++] = a;
			a.op    = OP_SYSCALL;
			v[vc++] = a;
			break;
		default:
			EXIT(1, "Unknown line type (%d)", f->lines[i]->type);
		}
	}
	// Restore stack pointer
	a.r2.op = OP_MOV;
	a.r2.r0= 31;
	a.r2.r1= 30;
	v[vc++] = a;
	a.r.op = OP_POP;
	a.r.r  = 30;
	v[vc++] = a;
	v[vc++].op = OP_RET;
	*vasms     = realloc(v, vc * sizeof *v);
	*vasmcount = vc;
	return 0;
}

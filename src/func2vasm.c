#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "func.h"
#include "hashtbl.h"
#include "util.h"


int func2vasm(union vasm_all **vasms, size_t *vasmcount, struct func *f) {
	size_t vc = 0, vs = 1024;
	union vasm_all *v = malloc(vs * sizeof *v);
	union vasm_all a;
	union func_line_all_p l;

	*vasms = malloc(1024 * sizeof **vasms);

	a.s.op  = VASM_OP_LABEL;
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
		case FUNC_LINE_ASSIGN:
			if (l.a->cons) {
				consts[constcount] = i;
				constcount++;
			}
			break;
		case FUNC_LINE_MATH:
			if (isnum(*l.m->y)) {
				consts[constcount] = i;
				constcount++;
			}
			if (l.m->z != NULL && isnum(*l.m->z)) {
				consts[constcount] = i;
				constcount++;
			}
			break;
		}
	}

	if (constcount >= sizeof consts / sizeof *consts)
		constcount = 0;

	struct hashtbl constvalh;
	h_create(&constvalh, 16);
	for (size_t i = f->argcount; i < constcount; i++) {
		l.line   = f->lines[consts[i]];
		a.op     = VASM_OP_SET;
		a.rs.r   = i;
		const char *key, *val;
		char b[64];
		static size_t bc = 0;
		char use_y;
		switch (l.line->type) {
		case FUNC_LINE_ASSIGN:
			key = l.a->var;
			val = l.a->value;
			break;
		case FUNC_LINE_MATH:
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
		}
		const char *ok = (const char *)h_get(&constvalh, val);
		if (ok != (char *)-1) {
			switch (l.line->type) {
			case FUNC_LINE_ASSIGN:
				break;
			case FUNC_LINE_MATH:
				if (use_y)
					l.m->y = ok;
				else
					l.m->z = ok;
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
		struct func_line_return *flr;
		size_t ra, rb, reg;
		switch (f->lines[i]->type) {
		case FUNC_LINE_ASSIGN:
			if (fl.a->cons && h_get(&tbl, fl.a->var) != -1)
				break;
			reg = h_get(&tbl, fl.a->var);
			if (reg == -1) {
				ERROR("Variable '%s' not declared", fl.a->var);
				EXIT(1);
			}
			if ('0' <= *fl.a->var && *fl.a->var <= '9') {
				ERROR("You can't assign to a number");
				EXIT(1);
			}
			if ('0' <= *fl.a->value && *fl.a->value <= '9') {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = reg;
				a.rs.str = fl.a->value;
			} else {
				a.r2.op   = VASM_OP_MOV;
				a.r2.r[0] = reg;
				reg = h_get(&tbl, fl.a->value);
				if (reg == -1) {
					ERROR("Variable '%s' not declared", fl.a->value);
					EXIT(1);
				}
				a.r2.r[1] = reg;
			}
			v[vc++] = a;
			break;
		case FUNC_LINE_DECLARE:
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
			break;
		case FUNC_LINE_DESTROY:
			// TODO
			if (fl.d->var[1] == '.')
				break;
			reg = h_get(&tbl, fl.d->var);
			if (reg == -1) {
				ERROR("Variable '%s' not declared", fl.d->var);
				EXIT(1);
			}
			allocated_regs[reg] = 0;
			h_rem(&tbl, fl.d->var);
			break;
		case FUNC_LINE_FUNC:
			flf = (struct func_line_func *)f->lines[i];

			for (size_t j = flf->var ? 1 : 0; j < 32; j++) {
				if (allocated_regs[j]) {
					a.r.op = VASM_OP_PUSH;
					a.r.r  = j;
					v[vc++] = a;
				}
			}
			for (size_t j = 0; j < flf->paramcount; j++) {
				size_t r = h_get(&tbl, flf->params[j]);
				if (r == -1) {
					a.rs.op  = VASM_OP_SET;
					a.rs.r   = j;
					a.rs.str = flf->params[j];
				} else {
					a.r2.op   = VASM_OP_MOV;
					a.r2.r[0] = j;
					a.r2.r[1] = r;
				}
				v[vc++] = a;
			}

			// Call
			a.s.op  = VASM_OP_CALL;
			a.s.str = flf->name;
			v[vc++] = a;

			// Pop registers
			for (int j = 31; j >= (flf->var ? 1 : 0); j--) {
				if (allocated_regs[j]) {
					a.r.op = VASM_OP_POP;
					a.r.r  = j;
					v[vc++] = a;
				}
			}
			break;
		case FUNC_LINE_GOTO:
			flg = (struct func_line_goto *)f->lines[i];
			a.s.op  = VASM_OP_JMP;
			a.s.str = flg->label;
			v[vc++] = a;
			break;
		case FUNC_LINE_IF:
			fli = (struct func_line_if *)f->lines[i];
			if (isnum(*fli->var)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = fli->var;
				v[vc++] = a;
			} else {
				ra = h_get(&tbl, fli->var);
				if (ra == -1) {
					ERROR("Variable '%s' not declared", fl.i->var);
					EXIT(1);
				}
			}
			if (ra < constcount) {
				char jz = (fli->inv == VASM_OP_JZ);
				if (is_const_reg_zero[ra] != jz)
					break;
				a.s.op  = VASM_OP_JMP;
				a.s.str = fli->label;
			} else {
				rb = 0;
				a.r2s.op   = fli->inv ? VASM_OP_JZ : VASM_OP_JNZ;
				a.r2s.r[0] = ra;
				a.r2s.r[1] = rb;
				a.r2s.str  = fli->label;
			}
			v[vc++] = a;
			break;
		case FUNC_LINE_LABEL:
			fll = (struct func_line_label *)f->lines[i];
			a.s.op  = VASM_OP_LABEL;
			a.s.str = fll->label;
			v[vc++] = a;
			break;
		case FUNC_LINE_MATH:
			flm = (struct func_line_math *)f->lines[i];
			if (isnum(*flm->x)) {
				ERROR("You can't assign to a number");
				EXIT(1);
			}
			if (isnum(*flm->y)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = flm->y;
				v[vc++] = a;
			} else {
				ra = h_get(&tbl, flm->y);
				if (ra == -1) {
					ERROR("Variable '%s' not declared", flm->y);
					EXIT(1);
				}
			}
			if (flm->op != MATH_INV && flm->op != MATH_NOT) {
				if (isnum(*flm->z)) {
					a.rs.op  = VASM_OP_SET;
					a.rs.r   = rb = 21;
					a.rs.str = flm->z;
					v[vc++] = a;
				} else {
					// TODO
					if (flm->z[1] == '.') {
						char l[64], r[64];
						memcpy(l, flm->z, 1);
						l[1] = 0;
						memcpy(r, flm->z + 2, strlen(flm->z) - 2);
						r[strlen(flm->z) - 2] = 0;

						a.rs.op  = VASM_OP_SET;
						a.rs.r   = 22;
						a.rs.str = "-1";
						v[vc++] = a;

						rb = 21;
						a.r3.op   = VASM_OP_LOADLAT;
						a.r3.r[0] = rb;
						a.r3.r[1] = h_get(&tbl, l);
						a.r3.r[2] = 22;
						v[vc++] = a;
					} else {
						// TODO
						rb = h_get(&tbl, flm->z);
						if (rb == -1) {
							ERROR("Variable '%s' not declared", flm->z);
							EXIT(1);
						}
					}
				}
				a.r3.op   = flm->op;
				a.r3.r[0] = h_get(&tbl, flm->x);
				a.r3.r[1] = ra;
				a.r3.r[2] = rb;
				if (a.r3.r[0] == -1) {
					size_t reg = 0;
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
		case FUNC_LINE_RETURN:
			flr = (struct func_line_return *)f->lines[i];
			a.rs.op  = VASM_OP_SET;
			a.rs.r   = 0;
			a.rs.str = flr->val;
			v[vc++] = a;
			v[vc++].op = VASM_OP_RET;
			break;
		case FUNC_LINE_STORE:
			if (isnum(*fl.s->var)) {
				ERROR("You can't index a number");
				EXIT(1);
			}
			if (isnum(*fl.s->val)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = fl.s->val;
				v[vc++] = a;
			} else {
				ra = h_get(&tbl, fl.s->val);
				if (ra == -1) {
					ERROR("Variable '%s' not declared", fl.s->val);
					EXIT(1);
				}
			}
			if (isnum(*fl.s->index)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = rb = 21;
				a.rs.str = fl.s->val;
				v[vc++] = a;
			} else {
				rb = h_get(&tbl, fl.s->index);
				if (rb == -1) {
					ERROR("Variable '%s' not declared", fl.s->index);
					EXIT(1);
				}
			}
			a.r2.op = VASM_OP_STOREBAT; // TODO determine required length
			a.r2.r[0] = ra;
			a.r2.r[1] = h_get(&tbl, fl.s->var);
			a.r2.r[2] = rb;
			if (a.r2.r[1] == -1) {
				ERROR("Variable '%s' not declared", fl.s->var);
				EXIT(1);
			}
			break;
		default:
			ERROR("Unknown line type (%d)", f->lines[i]->type);
			EXIT(1);
		}
	}
	a.op       = VASM_OP_RET;
	v[vc++]    = a;
	*vasms     = realloc(v, vc * sizeof *v);
	*vasmcount = vc;
	return 0;
}

/**
 * Steps:
 *   0. Parse text into lines
 *   1. Parse lines into structs
 *   2. Optimize structs
 *   3. Parse funcs into virt assembly
 *   4. Optimize virt assembly
 *   5. Convert virt assembly to real assembly
 *   6. Optimize real assembly
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "hashtbl.h"


#define FUNC_LINE_NONE   0
#define FUNC_LINE_ASSIGN 1
#define FUNC_LINE_FUNC   2
#define FUNC_LINE_GOTO   3
#define FUNC_LINE_IF     4
#define FUNC_LINE_LABEL  5
#define FUNC_LINE_MATH   6
#define FUNC_LINE_RETURN 7

#define MATH_ADD VASM_OP_ADD
#define MATH_SUB VASM_OP_SUB
#define MATH_MUL VASM_OP_MUL
#define MATH_DIV VASM_OP_DIV
#define MATH_MOD VASM_OP_MOD
#define MATH_NOT VASM_OP_NOT
#define MATH_INV VASM_OP_INV


//#define streq(x,y) (strcmp(x,y) == 0)
#define isnum(c) ('0' <= c && c <= '9')


struct variable {
	char type[32];
	char name[32];
};

struct func_line {
	char type;
};

struct func_line_assign {
	struct func_line line;
	char *var;
	char *value;
};

struct func_line_func {
	struct func_line line;
	unsigned char paramcount;
	struct variable  assign;
	char name[32];
	char params[32][32];
};

struct func_line_goto {
	struct func_line line;
	char *label;
};

struct func_line_if {
	struct func_line line;
	char *label;
	char *var;
};

struct func_line_label {
	struct func_line line;
	char *label;
};

struct func_line_math {
	struct func_line line;
	char op;
	char *x, *y, *z;
};

struct func_line_return {
	struct func_line line;
	char *val;
};

union func_line_all_p {
	struct func_line *line;
	struct func_line_assign *a;
	struct func_line_func   *f;
	struct func_line_goto   *g;
	struct func_line_if     *i;
	struct func_line_label  *l;
	struct func_line_math   *m;
	struct func_line_return *r;
};


struct func_arg {
	char type[32];
	char name[32];
};


struct func {
	char type[32];
	char name[32];
	unsigned char argcount;
	unsigned char linecount;
	struct func_arg   args[16];
	struct func_line *lines[256];
};

struct func funcs[4096];
size_t funccount;

struct vasm vasms[4096];
size_t vasmcount;



static int func2vasm(struct func *f) {
	union vasm_all a;
	a.s.op  = VASM_OP_LABEL;
	a.s.str = f->name;
	vasms[vasmcount] = a.a;
	vasmcount++;

	struct hashtbl tbl;
	if (h_create(&tbl, 8) < 0) {
		perror("Failed to create hash table");
		return -1;
	}

	char allocated_regs[32];
	memset(allocated_regs, 0, sizeof allocated_regs);

	for (size_t i = 0; i < f->linecount; i++) {
		struct func_line_assign *fla;
		struct func_line_func   *flf;
		struct func_line_goto   *flg;
		struct func_line_if     *fli;
		struct func_line_label  *fll;
		struct func_line_math   *flm;
		struct func_line_return *flr;
		char ra, rb;
		switch (f->lines[i]->type) {
		case FUNC_LINE_ASSIGN:
			fla = (struct func_line_assign *)f->lines[i];
			if ('0' <= *fla->var && *fla->var <= '9') {
				fprintf(stderr, "You can't assign to a number\n");
				abort();
			}
			size_t reg = 0;
			for ( ; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
				if (!allocated_regs[reg]) {
					allocated_regs[reg] = 1;
					break;
				}
			}
			if (h_add(&tbl, fla->var, reg) < 0) {
				fprintf(stderr, "Failed to add variable to hashtable\n");
				abort();
			}
			if ('0' <= *fla->value && *fla->value <= '9') {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = reg;
				a.rs.str = fla->value;
			} else {
				a.r2.op   = VASM_OP_MOV;
				a.r2.r[0] = reg;
				reg = h_get(&tbl, fla->value);
				if (reg == -1) {
					fprintf(stderr, "Variable not defined\n");
					abort();
				}
				a.r2.r[1] = reg;
			}
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_FUNC:
			flf = (struct func_line_func *)f->lines[i];

			for (size_t j = 0; j < 32; j++) {
				if (allocated_regs[j]) {
					a.r.op = VASM_OP_PUSH;
					a.r.r  = j;
					vasms[vasmcount] = a.a;
					vasmcount++;
				}
			}
			for (size_t j = 0; j < flf->paramcount; j++) {
				if (isnum(*flf->params[j])) {
					a.rs.op  = VASM_OP_SET;
					a.rs.r   = j;
					a.rs.str = flf->params[j];
				} else {
					size_t r = h_get(&tbl, flf->params[j]);
					if (r == -1) {
						fprintf(stderr, "Var akakaka %s\n", flf->params[j]);
						break;
					}
					a.r2.op   = VASM_OP_MOV;
					a.r2.r[0] = j;
					a.r2.r[1] = r;
				}
				vasms[vasmcount] = a.a;
				vasmcount++;
			}

			// Call
			a.s.op  = VASM_OP_CALL;
			a.s.str = flf->name;
			vasms[vasmcount] = a.a;
			vasmcount++;

			// Pop registers
			for (int j = 31; j >= 0; j--) {
				if (allocated_regs[j]) {
					a.r.op = VASM_OP_POP;
					a.r.r  = j;
					vasms[vasmcount] = a.a;
					vasmcount++;
				}
			}
			break;
		case FUNC_LINE_GOTO:
			flg = (struct func_line_goto *)f->lines[i];
			a.s.op  = VASM_OP_JMP;
			a.s.str = flg->label;
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_IF:
			fli = (struct func_line_if *)f->lines[i];
			if (isnum(*fli->var)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = fli->var;
				vasms[vasmcount] = a.a;
				vasmcount++;
			} else {
				ra = h_get(&tbl, fli->var);
				if (ra == -1) {
					fprintf(stderr, "Variable not defined: '%s'\n", fli->var);
					abort();
				}
			}
			rb = 0;
			a.r2s.op   = VASM_OP_JNZ;
			a.r2s.r[0] = ra;
			a.r2s.r[1] = rb;
			a.r2s.str  = fli->label;
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_LABEL:
			fll = (struct func_line_label *)f->lines[i];
			a.s.op  = VASM_OP_LABEL;
			a.s.str = fll->label;
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_MATH:
			flm = (struct func_line_math *)f->lines[i];
			if (isnum(*flm->x)) {
				fprintf(stderr, "You can't assign to a number\n");
				abort();
			}
			if (isnum(*flm->y)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = flm->y;
				vasms[vasmcount] = a.a;
				vasmcount++;
			} else {
				ra = h_get(&tbl, flm->y);
				if (ra == -1) {
					fprintf(stderr, "RIPOfzkzefopozezERZ\n");
					abort();
				}
			}
			if (flm->op != MATH_INV) {
				if (isnum(*flm->z)) {
					a.rs.op  = VASM_OP_SET;
					a.rs.r   = rb = 21;
					a.rs.str = flm->z;
					vasms[vasmcount] = a.a;
					vasmcount++;
				} else {
					rb = h_get(&tbl, flm->z);
					if (rb == -1) {
						fprintf(stderr, "RIPOERefnzfezfzeZ\n");
						abort();
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
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_RETURN:
			flr = (struct func_line_return *)f->lines[i];
			a.rs.op  = VASM_OP_SET;
			a.rs.r   = 0;
			a.rs.str = flr->val;
			vasms[vasmcount] = a.a;
			vasmcount++;
			vasms[vasmcount].op = VASM_OP_RET;
			vasmcount++;
			break;
		default:
			fprintf(stderr, "Unknown line type (%d)\n", f->lines[i]->type);
			abort();
		}
	}
	a.a.op  = VASM_OP_RET;
	vasms[vasmcount] = a.a;
	vasmcount++;
	return 0;
}



int structs2vasm() {
	for (size_t i = 0; i < funccount; i++)
		func2vasm(&funcs[i]);
}

#include "optimize/vasm.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "hashtbl.h"
#include "util.h"


static int optimizevasm_replace(union vasm_all *vasms, size_t *vasmcount)
{
	for (size_t i = 0; i < *vasmcount; i++) {
		union vasm_all a = vasms[i];
		size_t reg;
		switch (a.op) {
		case VASM_OP_SET:
			break; // TODO
			reg = a.r.r;
			for (size_t j = i + 1; j < *vasmcount; j++) {
				a = vasms[j];
			}
			break;
		case VASM_OP_POP:
			reg = a.r.r;
			a = vasms[i + 1];
			if (a.op == VASM_OP_PUSH && a.r.r == reg) {
				for (size_t j = i + 2; j < *vasmcount; j++) {
					a = vasms[j];
					switch (a.op) {
					case VASM_OP_SET:
						if (a.rs.r == reg)
							goto unused;
						break;
					case VASM_OP_POP:
						if (a.r.r == reg)
							goto unused;
						break;
					case VASM_OP_PUSH:
					case VASM_OP_JZ:
					case VASM_OP_JNZ:
					//case VASM_OP_JG:
					//case VASM_OP_JGE:
						if (a.r.r == reg)
							goto used;
						break;
					case VASM_OP_ADD:
					case VASM_OP_SUB:
					case VASM_OP_MUL:
					case VASM_OP_DIV:
					case VASM_OP_MOD:
					case VASM_OP_REM:
					case VASM_OP_RSHIFT:
					case VASM_OP_LSHIFT:
					case VASM_OP_AND:
					case VASM_OP_OR:
					case VASM_OP_XOR:
						if (a.r3.r[1] == reg || a.r3.r[2] == reg)
							goto used;
						if (a.r3.r[0] == reg)
							goto unused;
						break;
					case VASM_OP_MOV:
					case VASM_OP_NOT:
					case VASM_OP_INV:
						if (a.r3.r[1] == reg)
							goto used;
						if (a.r3.r[0] == reg)
							goto unused;
						break;
					}
				}
			unused:
				*vasmcount -= 2;
				memmove(vasms + i, vasms + i + 2, (*vasmcount - i) * sizeof *vasms);
				i -= 4;
			used:
				;
			}
			break;
		case VASM_OP_MOV:
			if (a.r2.r[0] == a.r2.r[1]) {
				(*vasmcount)--;
				memmove(vasms + i, vasms + i + 1, (*vasmcount - i) * sizeof *vasms);
				i--;
			}
			break;
		}
	}

	return 0;
}


static int optimizevasm_peephole2(union vasm_all *vasms, size_t *vasmcount)
{
	for (size_t i = 0; i + 1 < *vasmcount; i++) {
		union vasm_all a0 = vasms[i + 0],
		               a1 = vasms[i + 1];
		switch (a0.op) {
		case VASM_OP_RET:
			if (a1.op == VASM_OP_RET) {
				(*vasmcount)--;
				memmove(vasms + i + 1, vasms + i + 3, (*vasmcount - i) * sizeof vasms[i]);
				i -= 2;
			}
			break;
		}
	}

	return 0;
}


static int optimizevasm_peephole3(union vasm_all *vasms, size_t *vasmcount)
{
	for (size_t i = 0; i + 2 < *vasmcount; i++) {
		union vasm_all a0 = vasms[i + 0],
		               a1 = vasms[i + 1],
		               a2 = vasms[i + 2];
		switch (a0.op) {
		case VASM_OP_JZ:
		case VASM_OP_JNZ:
			if (a1.op == VASM_OP_JMP   &&
			    a2.op == VASM_OP_LABEL &&
			    streq(a0.rs.str, a2.s.str)) {
				a0.op = (a0.op == VASM_OP_JZ) ? VASM_OP_JNZ : VASM_OP_JZ;
				a0.rs.str = a1.s.str;
				vasms[i] = a0;
				(*vasmcount)--;
				memmove(vasms + i + 1, vasms + i + 2, (*vasmcount - i) * sizeof vasms[i]);
				i -= 3;
			}
			break;
		}
	}

	return 0;
}


static int optimizevasm_peephole4(union vasm_all *vasms, size_t *vasmcount)
{
	for (size_t i = 0; i + 3 < *vasmcount; i++) {
		union vasm_all a0 = vasms[i + 0],
		               a1 = vasms[i + 1],
		               a2 = vasms[i + 2],
		               a3 = vasms[i + 3];
		switch (a0.op) {
		case VASM_OP_PUSH:
			if (a1.op == VASM_OP_CALL &&
			    a2.op == VASM_OP_POP  &&
			    a0.r.r == a2.r.r) {
				switch (a3.op) {
				case VASM_OP_SET:
					if (a3.rs.r == a0.r.r)
						goto remove_push;
					goto dont_remove_push;
				}
			}
			goto dont_remove_push;
		remove_push:
			*vasmcount -= 2;
			vasms[i] = vasms[i + 1];
			memmove(vasms + i + 1, vasms + i + 3, (*vasmcount - i) * sizeof vasms[i]);
		dont_remove_push:
			break;
		}
	}

	return 0;
}


void optimizevasm(union vasm_all *vasm, size_t *vasmcount) {
	for (size_t i = 0; i < 5; i++) {
		optimizevasm_replace  (vasm, vasmcount);
		optimizevasm_peephole2(vasm, vasmcount);
		optimizevasm_peephole3(vasm, vasmcount);
		optimizevasm_peephole4(vasm, vasmcount);
	}
}

#include "optimize/vasm.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "hashtbl.h"
#include "util.h"


/**
 * Remove instructions that are unreachable.
 */
static void _dead_code_elimination(union vasm_all *vasms, size_t *vasmcount, size_t *i)
{
	size_t start = *i + 1, end = start;
	for (size_t j = start; j < *vasmcount; j++) {
		union vasm_all a = vasms[j];
		if (a.op == VASM_OP_LABEL)
			break;
		end = j + 1;
	}
	memmove(vasms + start, vasms + end, *vasmcount - start);
	*vasmcount -= end - start;
}


/**
 * Replace push/pop combinations with mov instructions
 */
static void _pushpop_to_mov(union vasm_all *vasms, size_t *vasmcount, size_t *i)
{
	union vasm_all a = vasms[*i];
	int pushr = a.r.r, popr;
	size_t pushi = *i, popi;
	// Find next pop instruction
	int stackdiff = 1;
	for (size_t j = pushi + 1; j < *vasmcount; j++) {
		a = vasms[j];
		if (a.op == VASM_OP_LABEL) {
			// Don't optimize what can potentially be magic
			return;
		}
		if (a.op == VASM_OP_CALL) {
			// Subroutines can change the registers to any value they like
			return;
		}
		if (a.op == VASM_OP_PUSH) {
			stackdiff++;
		}
		if (a.op == VASM_OP_POP) {
			stackdiff--;
			if (stackdiff == 0) {
				popr = a.r.r;
				popi = j;
				goto foundpop;
			}
		}
	}
	// I hope whoever wrote this assembly knows what (s)he is doing
	return;
foundpop:
	// Check if the pushed register is overwritten at some point
	// If it is, then the push is necessary unless the pop register
	// isn't used as an argument before the pop
	; int pushwritten = 0, popused = 0;
	for (size_t j = pushi + 1; j < popi; j++) {
		a = vasms[j];
		switch (get_vasm_args_type(a.op)) {
		case VASM_ARGS_TYPE_REG1:
			if (a.op == VASM_OP_PUSH) {
				if (a.r.r == popr)
					popused = 1;
			} else if (a.op == VASM_OP_POP) {
				if (a.r.r == pushr)
					pushwritten = 1;
			} else {
				DEBUG("There are 1 reg instructions other than push and pop?");
				DEBUG("Opcode: %d", a.op);
				DEBUG("Assuming the register will be overwritten or used as argument");
				pushwritten = popused = 1;
			}
			break;
		case VASM_ARGS_TYPE_REG2:
			if (a.r2.r[0] == pushr)
				pushwritten = 1;
			if (a.r2.r[1] == popr)
				popused = 1;
			break;
		case VASM_ARGS_TYPE_REG3:
			if (a.r3.r[0] == pushr)
				pushwritten = 1;
			if (a.r3.r[1] == popr || a.r3.r[2] == popr)
				popused = 1;
			break;
		case VASM_ARGS_TYPE_REGVAL:
			if (a.op == VASM_OP_SET && a.rs.r == pushr)
				pushwritten = 1;
			break;
		}
		if (pushwritten && popused)
			return;
	}
	if (pushwritten) {
		// Replace push and remove pop
		vasms[pushi].op = VASM_OP_MOV;
		vasms[pushi].r2.r[0] = popr;
		vasms[pushi].r2.r[1] = pushr;
		(*vasmcount)--;
		memmove(vasms + popi, vasms + popi + 1, (*vasmcount - popi) * sizeof *vasms);
	} else {
		// Replace pop and remove push
		vasms[popi].op = VASM_OP_MOV;
		vasms[popi].r2.r[0] = popr;
		vasms[popi].r2.r[1] = pushr;
		(*vasmcount)--;
		memmove(vasms + pushi, vasms + pushi + 1, (*vasmcount - pushi) * sizeof *vasms);
	}
}


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
		case VASM_OP_PUSH:
			if (1 & 1)
				_pushpop_to_mov(vasms, vasmcount, &i);
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
		case VASM_OP_JMP:
		case VASM_OP_RET:
			if (1 & 1) // TODO optimization options
				_dead_code_elimination(vasms, vasmcount, &i);
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

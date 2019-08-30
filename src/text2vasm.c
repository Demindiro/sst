#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include "text2vasm.h"
#include "vasm.h"


#define FUNC_LINE_NONE   0
#define FUNC_LINE_FUNC   1


struct lblpos lbl2pos[4096];
size_t lbl2poscount;
struct lblpos pos2lbl[4096];
size_t pos2lblcount;


static int getop(char *mnem)
{
	char c = *mnem;
	char *ptr = mnem;
	switch (c) {
	case 'a':
		if (streq("add", mnem))
			return VASM_OP_ADD;
		break;
	case 'c':
		if (streq("call", mnem))
			return VASM_OP_CALL;
		break;
	case 'd':
		if (streq("div", mnem))
			return VASM_OP_DIV;
		break;
	case 'i':
		if (streq("inv", mnem))
			return VASM_OP_INV;
		break;
	case 'j':
		if (streq("jmp", mnem))
			return VASM_OP_JMP;
		if (streq("jz", mnem))
			return VASM_OP_JZ;
		if (streq("jnz", mnem))
			return VASM_OP_JNZ;
		break;
	case 'l':
		if (streq("loadl", mnem))
			return VASM_OP_LOADL;
		if (streq("loadi", mnem))
			return VASM_OP_LOADI;
		if (streq("loads", mnem))
			return VASM_OP_LOADS;
		if (streq("loadb", mnem))
			return VASM_OP_LOADB;
		if (streq("loadlat", mnem))
			return VASM_OP_LOADLAT;
		if (streq("loadiat", mnem))
			return VASM_OP_LOADIAT;
		if (streq("loadsat", mnem))
			return VASM_OP_LOADSAT;
		if (streq("loadbat", mnem))
			return VASM_OP_LOADBAT;
		if (streq("lshift", mnem))
			return VASM_OP_LSHIFT;
		if (streq("less", mnem))
			return VASM_OP_LESS;
		if (streq("lesse", mnem))
			return VASM_OP_LESSE;
		break;
	case 'm':
		if (streq("mov", mnem))
			return VASM_OP_MOV;
		if (streq("mod", mnem))
			return VASM_OP_MOD;
		break;
	case 'n':
		if (streq("not", mnem))
			return VASM_OP_NOT;
		break;
	case 'p':
		if (streq("push", mnem))
			return VASM_OP_PUSH;
		if (streq("pop", mnem))
			return VASM_OP_POP;
		break;
	case 'r':
		if (streq("ret", mnem))
			return VASM_OP_RET;
		if (streq("rshift", mnem))
			return VASM_OP_RSHIFT;
		break;
	case 's':
		if (streq("set", mnem))
			return VASM_OP_SET;
		if (streq("setl", mnem))
			return VASM_OP_SETL;
		if (streq("seti", mnem))
			return VASM_OP_SETI;
		if (streq("sets", mnem))
			return VASM_OP_SETS;
		if (streq("setb", mnem))
			return VASM_OP_SETB;
		if (streq("storel", mnem))
			return VASM_OP_STOREL;
		if (streq("storei", mnem))
			return VASM_OP_STOREI;
		if (streq("stores", mnem))
			return VASM_OP_STORES;
		if (streq("storeb", mnem))
			return VASM_OP_STOREB;
		if (streq("storelat", mnem))
			return VASM_OP_STORELAT;
		if (streq("storeiat", mnem))
			return VASM_OP_STOREIAT;
		if (streq("storesat", mnem))
			return VASM_OP_STORESAT;
		if (streq("storebat", mnem))
			return VASM_OP_STOREBAT;
		if (streq("sub", mnem))
			return VASM_OP_SUB;
		if (streq("syscall", mnem))
			return VASM_OP_SYSCALL;
		break;
	case 'x':
		if (streq("xor", mnem))
			return VASM_OP_XOR;
		break;
	case '.':
		if (streq(".long", mnem))
			return VASM_OP_RAW_LONG;
		if (streq(".int", mnem))
			return VASM_OP_RAW_INT;
		if (streq(".short", mnem))
			return VASM_OP_RAW_SHORT;
		if (streq(".byte", mnem))
			return VASM_OP_RAW_BYTE;
		if (streq(".str", mnem))
			return VASM_OP_RAW_STR;
		break;
	}

	if (*ptr == '#')
		return VASM_OP_COMMENT;
	while (*ptr != 0)
	       ptr++;
	ptr--;
	if (*ptr == ':')
		return VASM_OP_LABEL;

	printf("Invalid mnemonic (%s)\n", mnem);
	return -1;
}


static int parse_op_args(union vasm_all *v, char *args)
{
#ifdef STR
# undef STR
#endif
	#define STR(assign) do {          \
		char *start = ptr;        \
		while (*ptr != ' ' && *ptr != '\t' && *ptr != ',' && *ptr != 0) \
			ptr++;            \
		size_t l = ptr - start;   \
		char *m  = malloc(l + 1); \
		memcpy(m, start, l);      \
		m[l] = 0;                 \
		assign = m;               \
	} while (0)
	#define REG(assign) do {    \
		if (*ptr != 'r') {  \
			printf("Argument is not a register (%s)\n", ptr); \
			return -1;  \
		}                   \
		ptr++;              \
		char *start = ptr;  \
		while ('0' <= *ptr && *ptr <= '9') \
			ptr++;      \
		num = strtol(start, NULL, 10);  \
		if (num >= 32) {    \
			printf("Register index out of range\n"); \
			return -1;  \
		}                   \
		assign = num;       \
	} while (0)
	#define SKIP \
		while (*ptr == ' ' || *ptr == '\t') \
			ptr++; \
		if (*ptr != ',') { \
			printf("Expected ',' (got %c)\n", *ptr); \
			return -1;              \
		}                               \
		ptr++;                          \
		while (*ptr == ' ' || *ptr == '\t') \
			ptr++

	char *ptr = args;

	long num;

	switch ((char)v->op) {
	// Nothing
	case VASM_OP_SYSCALL:
	case VASM_OP_RET:
		break;
	// 1 addr
	case VASM_OP_JMP:
	case VASM_OP_CALL:
		STR(v->s.str);
		break;
	// 1 reg
	case VASM_OP_PUSH:
	case VASM_OP_POP:
		REG(v->r.r);
		break;
	// 1 reg, 1 addr
	case VASM_OP_SET:
		REG(v->rs.r);
		SKIP;
		STR(v->rs.str);
		break;
	// 1 addr, 1 reg
	case VASM_OP_JZ:
	case VASM_OP_JNZ:
	case VASM_OP_JP:
	case VASM_OP_JPZ:
		STR(v->rs.str);
		SKIP;
		REG(v->rs.r);
		break;
	// 2 reg, 1 addr
		STR(v->r2s.str);
		SKIP;
		REG(v->r2s.r[0]);
		SKIP;
		REG(v->r2s.r[1]);
		break;
	// 2 reg
	case VASM_OP_STOREL:
	case VASM_OP_STOREI:
	case VASM_OP_STORES:
	case VASM_OP_STOREB:
	case VASM_OP_LOADL:
	case VASM_OP_LOADI:
	case VASM_OP_LOADS:
	case VASM_OP_LOADB:
	case VASM_OP_MOV:
	case VASM_OP_NOT:
	case VASM_OP_INV:
		REG(v->r2.r[0]);
		SKIP;
		REG(v->r2.r[1]);
		break;
	// 3 reg
	case VASM_OP_ADD:
	case VASM_OP_SUB:
	case VASM_OP_MUL:
	case VASM_OP_DIV:
	case VASM_OP_MOD:
	case VASM_OP_REM:
	case VASM_OP_RSHIFT:
	case VASM_OP_LSHIFT:
	case VASM_OP_XOR:
	case VASM_OP_STORELAT:
	case VASM_OP_STOREIAT:
	case VASM_OP_STORESAT:
	case VASM_OP_STOREBAT:
	case VASM_OP_LOADLAT:
	case VASM_OP_LOADIAT:
	case VASM_OP_LOADSAT:
	case VASM_OP_LOADBAT:
	case VASM_OP_LESS:
	case VASM_OP_LESSE:
		REG(v->r3.r[0]);
		SKIP;
		REG(v->r3.r[1]);
		SKIP;
		REG(v->r3.r[2]);
		break;
	// Raw (aka str)
	case VASM_OP_RAW_LONG:
	case VASM_OP_RAW_INT:
	case VASM_OP_RAW_SHORT:
	case VASM_OP_RAW_BYTE:
		STR(v->s.str);
		break;
	// Special case of STR
	case VASM_OP_RAW_STR:
		if (*ptr != '"') {
			printf("Strings must start with '\"'\n");
			return -1;
		}
		ptr++;
		char *start = ptr;
		while (*ptr != '"') {
			if (*ptr == 0) {
				printf("Strings must end with '\"'\n");
				return -1;
			}
			ptr++;
		}
		size_t l = ptr - start;
		char *m  = malloc(l + 1);
		memcpy(m, start, l);
		m[l] = 0;
		v->s.str = m;
		break;
	// Do nothing
	case VASM_OP_LABEL:
		break;
	// idk
	default:
		printf("Invalid op (%d)\n", v->op);
		return -1;
	}

	return 0;
}


int text2vasm(char *buf, size_t len, union vasm_all *vasms, size_t *vasmcount_p)
{
	size_t vasmcount = 0;

	char *ptr = buf;
	while (ptr - buf < len) {

		// Skip whitespace
		while (*ptr == '\n' || *ptr == '\t' || *ptr == ' ')
			ptr++;

		// Don't bother
		if (ptr - buf >= len)
			break;

		// Get mnemonic
		char *start = ptr;
		while (*ptr != '\n' && *ptr != '\t' && *ptr != ' ' && ptr - buf < len)
			ptr++;
		char b[64];
		size_t l = ptr - start;
		if (l >= sizeof b) {
			printf("Mnemonic is too large\n");
			return -1;
		}
		memcpy(b, start, l);
		b[l] = 0;
		int op = getop(b);
		if (op == -1)
			return op;
		if (op == VASM_OP_COMMENT) {
			while (*ptr != '\n' && *ptr != 0)
				ptr++;
			continue;
		}
		vasms[vasmcount].op = op;

		// Check if it is a label
		if (op == VASM_OP_LABEL) {
			char *m = malloc(l);
			memcpy(m, b, l - 1);
			m[l] = 0;
			vasms[vasmcount].s.str = m;
			vasmcount++;
			continue;
		}

		// Skip whitespace
		while (*ptr == '\t' || *ptr == ' ')
			ptr++;

		// Get arguments
		start = ptr;
		while (*ptr != '\n' && *ptr != '#' && ptr - buf < len)
			ptr++;
		l = ptr - start;
		if (l >= sizeof b) {
			printf("Argument list is too large\n");
			return -1;
		}
		memcpy(b, start, l);
		b[l] = 0;
		if (parse_op_args(&vasms[vasmcount], b) < 0) {
			printf("Failed to parse arguments\n");
			return -1;
		}

		vasmcount++;
	}

	*vasmcount_p = vasmcount;

	return 0;
}

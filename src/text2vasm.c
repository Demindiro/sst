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
			return OP_ADD;
		if (streq("and", mnem))
			return OP_AND;
		break;
	case 'c':
		if (streq("call", mnem))
			return OP_CALL;
		break;
	case 'd':
		if (streq("div", mnem))
			return OP_DIV;
		break;
	case 'i':
		if (streq("inv", mnem))
			return OP_INV;
		break;
	case 'j':
		if (streq("jmp", mnem))
			return OP_JMP;
		if (streq("jz", mnem))
			return OP_JZ;
		if (streq("jnz", mnem))
			return OP_JNZ;
		break;
	case 'l':
		if (streq("ldl", mnem))
			return OP_LDL;
		if (streq("ldi", mnem))
			return OP_LDI;
		if (streq("lds", mnem))
			return OP_LDS;
		if (streq("ldb", mnem))
			return OP_LDB;
		if (streq("ldlat", mnem))
			return OP_LDLAT;
		if (streq("ldiat", mnem))
			return OP_LDIAT;
		if (streq("ldsat", mnem))
			return OP_LDSAT;
		if (streq("ldbat", mnem))
			return OP_LDBAT;
		if (streq("lshift", mnem))
			return OP_LSHIFT;
		if (streq("less", mnem))
			return OP_LESS;
		if (streq("lesse", mnem))
			return OP_LESSE;
		break;
	case 'm':
		if (streq("mov", mnem))
			return OP_MOV;
		if (streq("mod", mnem))
			return OP_MOD;
		if (streq("mul", mnem))
			return OP_MUL;
		break;
	case 'n':
		if (streq("not", mnem))
			return OP_NOT;
		break;
	case 'p':
		if (streq("push", mnem))
			return OP_PUSH;
		if (streq("pop", mnem))
			return OP_POP;
		break;
	case 'r':
		if (streq("rem", mnem))
			return OP_REM;
		if (streq("ret", mnem))
			return OP_RET;
		if (streq("rshift", mnem))
			return OP_RSHIFT;
		break;
	case 's':
		if (streq("set", mnem))
			return OP_SET;
		if (streq("setl", mnem))
			return OP_SETL;
		if (streq("seti", mnem))
			return OP_SETI;
		if (streq("sets", mnem))
			return OP_SETS;
		if (streq("setb", mnem))
			return OP_SETB;
		if (streq("strl", mnem))
			return OP_STRL;
		if (streq("stri", mnem))
			return OP_STRI;
		if (streq("strs", mnem))
			return OP_STRS;
		if (streq("strb", mnem))
			return OP_STRB;
		if (streq("strlat", mnem))
			return OP_STRLAT;
		if (streq("striat", mnem))
			return OP_STRIAT;
		if (streq("strsat", mnem))
			return OP_STRSAT;
		if (streq("strbat", mnem))
			return OP_STRBAT;
		if (streq("sub", mnem))
			return OP_SUB;
		if (streq("syscall", mnem))
			return OP_SYSCALL;
		break;
	case 'x':
		if (streq("xor", mnem))
			return OP_XOR;
		break;
	case '.':
		if (streq(".long", mnem))
			return OP_RAW_LONG;
		if (streq(".int", mnem))
			return OP_RAW_INT;
		if (streq(".short", mnem))
			return OP_RAW_SHORT;
		if (streq(".byte", mnem))
			return OP_RAW_BYTE;
		if (streq(".str", mnem))
			return OP_RAW_STR;
		break;
	}

	if (*ptr == '#')
		return OP_COMMENT;
	while (*ptr != 0)
	       ptr++;
	ptr--;
	if (*ptr == ':')
		return OP_LABEL;

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

	switch(get_vasm_args_type(v->op)) {
	case ARGS_TYPE_NONE:
		break;
	case ARGS_TYPE_REG3:
		REG(v->r3.r[0]);
		SKIP;
		REG(v->r3.r[1]);
		SKIP;
		REG(v->r3.r[2]);
		break;
	case ARGS_TYPE_REG2:
		REG(v->r2.r[0]);
		SKIP;
		REG(v->r2.r[1]);
		break;
	case ARGS_TYPE_REG1:
		REG(v->r.r);
		break;
	case ARGS_TYPE_VAL:
		STR(v->s.str);
		break;
	case ARGS_TYPE_REGVAL:
		REG(v->rs.r);
		SKIP;
		STR(v->rs.str);
		break;
	case ARGS_TYPE_VALREG:
		STR(v->rs.str);
		SKIP;
		REG(v->rs.r);
		break;
	case ARGS_TYPE_SPECIAL:
		switch (v->op) {
		// Raw (aka str)
		case OP_RAW_LONG:
		case OP_RAW_INT:
		case OP_RAW_SHORT:
		case OP_RAW_BYTE:
			STR(v->s.str);
			break;
		// Special case of STR
		case OP_RAW_STR:
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
		case OP_LABEL:
			break;
		default:
			printf("Invalid special op (%d)\n", v->op);
			return -1;
		}
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
		if (op == OP_COMMENT) {
			while (*ptr != '\n' && *ptr != 0)
				ptr++;
			continue;
		}
		vasms[vasmcount].op = op;

		// Check if it is a label
		if (op == OP_LABEL) {
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

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include "vasm.h"


#define FUNC_LINE_NONE   0
#define FUNC_LINE_FUNC   1


union vasm_all vasms[4096];
size_t vasmcount;

char vbin[0x10000];
size_t vbinlen;

struct lblpos lbl2pos[4096];
size_t lbl2poscount;
struct lblpos pos2lbl[4096];
size_t pos2lblcount;


#define streq(a,b) (strcmp(a,b) == 0)


static int getop(char *mnem)
{
	printf("%s\n", mnem);
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
	case 'j':
		if (streq("jmp", mnem))
			return VASM_OP_JMP;
		if (streq("je", mnem))
			return VASM_OP_JE;
		if (streq("jne", mnem))
			return VASM_OP_JNE;
		break;
	case 'l':
		if (streq("load", mnem))
			return VASM_OP_LOAD;
		break;
	case 'm':
		if (streq("mov", mnem))
			return VASM_OP_MOV;
	case 'p':
		if (streq("push", mnem))
			return VASM_OP_PUSH;
		if (streq("pop", mnem))
			return VASM_OP_POP;
		break;
	case 'r':
		if (streq("ret", mnem))
			return VASM_OP_RET;
		break;
	case 's':
		if (streq("set", mnem))
			return VASM_OP_SET;
		if (streq("syscall", mnem))
			return VASM_OP_SYSCALL;
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
	// 2 reg, 1 addr
	case VASM_OP_JE:
	case VASM_OP_JNE:
	case VASM_OP_JG:
	case VASM_OP_JGE:
		STR(v->r2s.str);
		SKIP;
		REG(v->r2s.r[0]);
		SKIP;
		REG(v->r2s.r[1]);
		break;
	// 2 reg
	case VASM_OP_LOAD:
	case VASM_OP_MOV:
		REG(v->r2.r[0]);
		SKIP;
		REG(v->r2.r[1]);
		break;
	// 3 reg
	case VASM_OP_ADD:
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


static int text2vasm(char *buf, size_t len)
{
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
}



static int vasm2vbin() {

	#define POS2LBL(s) do {                      \
		pos2lbl[pos2lblcount].lbl = s;       \
		pos2lbl[pos2lblcount].pos = vbinlen; \
		pos2lblcount++;                      \
	} while (0)

	// Generate binary
	for (size_t i = 0; i < vasmcount; i++) {

		union vasm_all a = vasms[i];
		size_t val;

		if (a.op == (unsigned char)VASM_OP_LABEL) {
			lbl2pos[lbl2poscount].lbl = a.s.str;
			lbl2pos[lbl2poscount].pos = vbinlen;
			continue;
		}

		if (a.op == (unsigned char)VASM_OP_NONE  ||
		    a.op == (unsigned char)VASM_OP_COMMENT)
			continue;

		vbin[vbinlen] = vasms[i].op;
		vbinlen++;

		switch (a.op) {
		// No args
		case VASM_OP_SYSCALL:
		case VASM_OP_RET:
			break;
		// 1 reg
		case VASM_OP_SET:
			vbin[vbinlen] = a.rs.r;
			vbinlen++;
			if ('0' <= a.rs.str[0] && a.rs.str[0] <= '9')
				val = strtol(a.rs.str, NULL, 0);
			else 
				POS2LBL(a.rs.str);
			*(size_t *)(vbin + vbinlen) = htobe64(val);
			vbinlen += sizeof val;
			break;
		// 2 reg
		case VASM_OP_LOAD:
		case VASM_OP_MOV:
			vbin[vbinlen] = a.r2.r[0];
			vbinlen++;
			vbin[vbinlen] = a.r2.r[1];
			vbinlen++;
			break;
		// 3 reg
		case VASM_OP_ADD:
			for (size_t i = 0; i < 3; i++) {
				vbin[vbinlen] = a.r3.r[i];
				vbinlen++;
			}
			break;
		// 1 addr
		case VASM_OP_JMP:
		case VASM_OP_CALL:
			if ('0' <= a.rs.str[0] && a.rs.str[0] <= '9')
				val = strtol(a.rs.str, NULL, 0);
			else 
				POS2LBL(a.rs.str);
			*(size_t *)(vbin + vbinlen) = val;
			vbinlen += sizeof val;
			break;
		// 2 reg, 1 addr
		case VASM_OP_JE:
		case VASM_OP_JNE:
		case VASM_OP_JG:
		case VASM_OP_JGE:
			vbin[vbinlen] = a.r2.r[0];
			vbinlen++;
			vbin[vbinlen] = a.r2.r[1];
			vbinlen++;
			if ('0' <= a.rs.str[0] && a.rs.str[0] <= '9')
				val = strtol(a.rs.str, NULL, 0);
			else 
				POS2LBL(a.rs.str);
			*(size_t *)(vbin + vbinlen) = val;
			vbinlen += sizeof val;
			break;
		// Other
		case VASM_OP_RAW_LONG:
			vbinlen--;
			val = strtol(a.s.str, NULL, 0);
			*(unsigned long *)(vbin + vbinlen) = htobe64(val);
			vbinlen += sizeof (unsigned long);
			break;
		case VASM_OP_RAW_INT:
			vbinlen--;
			val = strtol(a.s.str, NULL, 0);
			*(unsigned int *)(vbin + vbinlen) = htobe32(val);
			vbinlen += sizeof (unsigned int);
			break;
		case VASM_OP_RAW_SHORT:
			vbinlen--;
			val = strtol(a.s.str, NULL, 0);
			*(unsigned short*)(vbin + vbinlen) = htobe16(val);
			vbinlen += sizeof (unsigned short);
			break;
		case VASM_OP_RAW_BYTE:
			vbinlen--;
			val = strtol(a.s.str, NULL, 0);
			*(unsigned char*)(vbin + vbinlen) = val;
			vbinlen += sizeof (unsigned char);
			break;
		case VASM_OP_RAW_STR:
			vbinlen--;
			val = strlen(a.s.str);
			memcpy(vbin + vbinlen, a.s.str, val);
			vbinlen += val;
			break;
		case VASM_OP_LABEL:
			vbinlen--;
			lbl2pos[lbl2poscount].lbl = a.s.str;
			lbl2pos[lbl2poscount].pos = vbinlen;
			lbl2poscount++;
			break;
		default:
			printf("IDK lol (%d)\n", a.op);
			break;
		}
	}

	// Fill in addresses
	// TODO
}



static int dumplbl(int fd)
{
	uint32_t v32 = htobe32(lbl2poscount);
	write(fd, &v32, sizeof v32);
	for (size_t i = 0; i < lbl2poscount; i++) {
		char b[260];
		size_t l = strlen(lbl2pos[i].lbl);
		if (l > 255) {
			printf("Label is too long");
			return -1;
		}
		b[0] = l;
		uint64_t pos = htobe64(lbl2pos[i].pos);
		memcpy(b + 1, lbl2pos[i].lbl, l);
		memcpy(b + 1 + l, &pos, sizeof pos);
		write(fd, b, 1 + l + sizeof pos);
	}
	
	v32 = htobe32(pos2lblcount);
	write(fd, &v32, sizeof v32);
	for (size_t i = 0; i < pos2lblcount; i++) {
		char b[260];
		size_t l = strlen(pos2lbl[i].lbl);
		if (l > 255) {
			printf("Label is too long");
			return -1;
		}
		b[0] = l;
		uint64_t pos = htobe64(pos2lbl[i].pos);
		memcpy(b + 1, pos2lbl[i].lbl, l);
		memcpy(b + 1 + l, &pos, sizeof pos);
		write(fd, b, 1 + l + sizeof pos);
	}

	return 0;
}



int main(int argc, char **argv) {
	
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <input> <output>", argv[0]);
		return 1;
	}

	// Read source
	char buf[0x10000];
	int fd = open(argv[1], O_RDONLY);
	size_t len = read(fd, buf, sizeof buf);
	close(fd);

	printf("=== text2vasm ===\n");
	if (text2vasm(buf, len) < 0)
		return 1;
	printf("\n");

	// x. vasm2vbin
	printf("=== vasm2vbin ===\n");
	vasm2vbin();
	printf("\n");
	for (size_t i = 0; i < lbl2poscount; i++)
		printf("%s --> %lu\n", lbl2pos[i].lbl, lbl2pos[i].pos);
	printf("\n");
	for (size_t i = 0; i < pos2lblcount; i++)
		printf("%lu --> %s\n", pos2lbl[i].pos, pos2lbl[i].lbl);
	printf("\n");
	size_t i = 0, j = 0;
	while (i < vbinlen) {
		for (size_t k = 0; k < 4 && i < vbinlen; k++) {
			for (size_t l = 0; l < 2 && i < vbinlen; l++) {
				printf("%02x", (unsigned char)vbin[i]);
				i++;
			}
			printf(" ");
		}
		for ( ; i % 8 != 0; i++) {
			if (i % 2 == 0)
				printf(" ");
			printf("  ");
		}
		printf("| ");
		for (size_t k = 0; k < 8 && j < vbinlen; k++) {
			char c = vbin[j];
			printf("%c", (' ' <= c && c <= '~') ? c : '.');
			j++;
		}
		printf("\n");
	}

	// Write binary shit
	fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0755);
	write(fd, "\x55\x10\x20\x19", 4); // Magic number
	dumplbl(fd);
	write(fd, vbin, vbinlen);
	close(fd);

	// Yay
	return 0;
}

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include "vasm.h"
#include "vasm2vbin.h"


int vasm2vbin(const union vasm_all *vasms, size_t vasmcount, char *vbin, size_t *vbinlen_p, struct lblmap *map)
{
	size_t vbinlen = 0;
	#define POS2LBL(s) do {                                \
		map->pos2lbl[map->pos2lblcount].lbl = s;       \
		map->pos2lbl[map->pos2lblcount].pos = vbinlen; \
		map->pos2lblcount++;                           \
	} while (0)

	// Generate binary
	for (size_t i = 0; i < vasmcount; i++) {

		union vasm_all a = vasms[i];
		size_t val;

		if (a.op == (unsigned char)VASM_OP_LABEL) {
			map->lbl2pos[map->lbl2poscount].lbl = a.s.str;
			map->lbl2pos[map->lbl2poscount].pos = vbinlen;
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
		case VASM_OP_PUSH:
		case VASM_OP_POP:
			vbin[vbinlen] = a.r.r;
			vbinlen++;
			break;
		// 2 reg
		case VASM_OP_STOREL:
		case VASM_OP_LOADL:
		case VASM_OP_MOV:
		case VASM_OP_NOT:
		case VASM_OP_INV:
			vbin[vbinlen] = a.r2.r[0];
			vbinlen++;
			vbin[vbinlen] = a.r2.r[1];
			vbinlen++;
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
		// 1 reg, 1 addr
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
		// 2 reg, 1 addr
		case VASM_OP_JZ:
		case VASM_OP_JNZ:
		case VASM_OP_JP:
		case VASM_OP_JPZ:
			vbin[vbinlen] = a.rs.r;
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
			map->lbl2pos[map->lbl2poscount].lbl = a.s.str;
			map->lbl2pos[map->lbl2poscount].pos = vbinlen;
			map->lbl2poscount++;
			break;
		default:
			printf("IDK lol (%d)\n", a.op);
			abort();
		}
	}

	// Fill in local addresses
	// TODO

	*vbinlen_p = vbinlen;
}


int dumplbl(int fd, struct lblmap *map)
{
	uint32_t v32 = htobe32(map->lbl2poscount);
	write(fd, &v32, sizeof v32);
	for (size_t i = 0; i < map->lbl2poscount; i++) {
		char b[260];
		size_t l = strlen(map->lbl2pos[i].lbl);
		if (l > 255) {
			printf("Label is too long");
			return -1;
		}
		b[0] = l;
		uint64_t pos = htobe64(map->lbl2pos[i].pos);
		memcpy(b + 1, map->lbl2pos[i].lbl, l);
		memcpy(b + 1 + l, &pos, sizeof pos);
		write(fd, b, 1 + l + sizeof pos);
	}
	
	v32 = htobe32(map->pos2lblcount);
	write(fd, &v32, sizeof v32);
	for (size_t i = 0; i < map->pos2lblcount; i++) {
		char b[260];
		size_t l = strlen(map->pos2lbl[i].lbl);
		if (l > 255) {
			printf("Label is too long");
			return -1;
		}
		b[0] = l;
		uint64_t pos = htobe64(map->pos2lbl[i].pos);
		memcpy(b + 1, map->pos2lbl[i].lbl, l);
		memcpy(b + 1 + l, &pos, sizeof pos);
		write(fd, b, 1 + l + sizeof pos);
	}

	return 0;
}

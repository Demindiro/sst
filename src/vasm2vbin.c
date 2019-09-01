#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include "util.h"
#include "vasm.h"
#include "vasm2vbin.h"



static size_t _getlblpos(const char *lbl, struct lblmap *map)
{
	for (size_t i = 0; i < map->lbl2poscount; i++) {
		if (streq(lbl, map->lbl2pos[i].lbl))
			return map->lbl2pos[i].pos;
	}
	return -1;
}

/**
 * Note that it is actually an estimate since a value can be between 1 and 8
 * bytes.
 */
static size_t _getoplen(struct vasm v)
{
	switch (get_vasm_args_type(v.op)) {
	case VASM_ARGS_TYPE_NONE:
		return 1;
	case VASM_ARGS_TYPE_REG1:
		return 2;
	case VASM_ARGS_TYPE_REG2:
		return 3;
	case VASM_ARGS_TYPE_REG3:
		return 4;
	case VASM_ARGS_TYPE_VAL:
		return 9;
	case VASM_ARGS_TYPE_REGVAL:
	case VASM_ARGS_TYPE_VALREG:
		return 10;
	case VASM_ARGS_TYPE_SPECIAL:
		switch (v.op) {
		case VASM_OP_RAW_BYTE:
			return 1;
		case VASM_OP_RAW_SHORT:
			return 2;
		case VASM_OP_RAW_INT:
			return 4;
		case VASM_OP_RAW_LONG:
			return 8;
		case VASM_OP_RAW_STR:
			; struct vasm_str *a = (struct vasm_str *)&v;
			return strlen(a->str);
			ERROR("TODO");
			EXIT(1);
		case VASM_OP_LABEL:
			return 0;
		default:
			ERROR("Unknown op '%d'", v.op);
			EXIT(1);
		}
	default:
		ERROR("Unknown op '%d'", v.op);
		EXIT(1);
	}
}


int vasm2vbin(const union vasm_all *vasms, size_t vasmcount, char *vbin, size_t *vbinlen_p, struct lblmap *map)
{
	size_t vbinlen = 0;
	#define POS2LBL(s) do {                                \
		map->pos2lbl[map->pos2lblcount].lbl = s;       \
		map->pos2lbl[map->pos2lblcount].pos = vbinlen; \
		map->pos2lblcount++;                           \
	} while (0)

	// Used for forward relative jumps
	struct lblpos jmprelmap[256];
	size_t        jmprelmapcount = 0;

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
		case VASM_OP_STRL:
		case VASM_OP_STRI:
		case VASM_OP_STRS:
		case VASM_OP_STRB:
		case VASM_OP_LDL:
		case VASM_OP_LDI:
		case VASM_OP_LDS:
		case VASM_OP_LDB:
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
		case VASM_OP_STRLAT:
		case VASM_OP_STRIAT:
		case VASM_OP_STRSAT:
		case VASM_OP_STRBAT:
		case VASM_OP_LDLAT:
		case VASM_OP_LDIAT:
		case VASM_OP_LDSAT:
		case VASM_OP_LDBAT:
		case VASM_OP_LESS:
		case VASM_OP_LESSE:
			for (size_t i = 0; i < 3; i++) {
				vbin[vbinlen] = a.r3.r[i];
				vbinlen++;
			}
			break;
		// 1 addr
		case VASM_OP_JMP:
			val = _getlblpos(a.rs.str, map);
			if (val != -1) {
				ssize_t d = (ssize_t)val - (ssize_t)vbinlen;
				if (-0x80 <= d && d <= 0x7F) {
					vbin[vbinlen - 1] = VASM_OP_JMPRB;
					vbin[vbinlen++  ] = (char)d;
					break;
				}
			}
		case VASM_OP_CALL:
			val = -1;
			if ('0' <= a.rs.str[0] && a.rs.str[0] <= '9')
				val = strtol(a.rs.str, NULL, 0);
			else 
				POS2LBL(a.rs.str);
			*(size_t *)(vbin + vbinlen) = val;
			vbinlen += sizeof val;
			break;
		// 1 reg, 1 addr
		case VASM_OP_SET:
		case VASM_OP_SETB:
		case VASM_OP_SETS:
		case VASM_OP_SETI:
		case VASM_OP_SETL:
			vbin[vbinlen] = a.rs.r;
			vbinlen++;
			val = -1;
			if ('0' <= a.rs.str[0] && a.rs.str[0] <= '9')
				val = strtol(a.rs.str, NULL, 0);
			else 
				POS2LBL(a.rs.str);
			if (a.op == VASM_OP_SET) {
#ifndef FORCE_SETL
				if (val <= 0xFF)
					a.op = VASM_OP_SETB;
				else if (val <= 0xFFFF)
					a.op = VASM_OP_SETS;
				else if (val <= 0xFFFFffff)
					a.op = VASM_OP_SETI;
				else if (val <= 0xFFFFffffFFFFffff)
					a.op = VASM_OP_SETL;
				else
					abort();
#else
				a.op = VASM_OP_SETL;
#endif
				vbin[vbinlen-2] = a.op;
			}
			switch (a.op) {
			case VASM_OP_SETB:
				if (val > 0xFF)
					abort();
				*(uint8_t *)(vbin + vbinlen) = val;
				vbinlen += 1;
				break;
			case VASM_OP_SETS:
				if (val > 0xFFFF)
					abort();
				*(uint16_t *)(vbin + vbinlen) = htobe16(val);
				vbinlen += 2;
				break;
			case VASM_OP_SETI:
				if (val > 0xFFFFffff)
					abort();
				*(uint32_t *)(vbin + vbinlen) = htobe32(val);
				vbinlen += 4;
				break;
			case VASM_OP_SETL:
				if (val > 0xFFFFffffFFFFffff)
					abort();
				*(uint64_t *)(vbin + vbinlen) = htobe64(val);
				vbinlen += 8;
				break;
			}
			break;
		// 1 reg, 1 addr
		case VASM_OP_JZ:
		case VASM_OP_JNZ:
		case VASM_OP_JP:
		case VASM_OP_JPZ:
			vbin[vbinlen] = a.rs.r;
			vbinlen++;
			val = _getlblpos(a.rs.str, map);
			if (val != -1) {
				ssize_t d = (ssize_t)val - (ssize_t)vbinlen;
				if (-0x80 <= d && d <= 0x7F) {
					char op;
					switch (a.op) {
					case VASM_OP_JZ : op = VASM_OP_JZB ; break;
					case VASM_OP_JNZ: op = VASM_OP_JNZB; break;
					case VASM_OP_JP : op = VASM_OP_JPB ; break;
					case VASM_OP_JPZ: op = VASM_OP_JPZB; break;
					default: assert(0);
					}
					vbin[vbinlen - 2] = op;
					vbin[vbinlen++  ] = (char)d;
					goto shortop;
				}
			} else {
				// Estimate distance
				size_t d = 3; // op (1) + reg (1) + offset (1)
				for (size_t j = i + 1; j < vasmcount; j++) {
					union vasm_all b = vasms[j];
					d += _getoplen(b.a);
					if (d > 0x7F)
						break;
					if (b.op == VASM_OP_LABEL &&
					    streq(a.rs.str, b.s.str)) {
						char op;
						switch (a.op) {
						case VASM_OP_JZ : op = VASM_OP_JZB ; break;
						case VASM_OP_JNZ: op = VASM_OP_JNZB; break;
						case VASM_OP_JP : op = VASM_OP_JPB ; break;
						case VASM_OP_JPZ: op = VASM_OP_JPZB; break;
						default: assert(0);
						}
						vbin[vbinlen - 2] = op;
						vbin[vbinlen++  ] = 0xFF;
						jmprelmap[jmprelmapcount].lbl = a.rs.str;
						jmprelmap[jmprelmapcount].pos = vbinlen - 1;
						jmprelmapcount++;
						goto shortop;
					}
				}
			}
			val = -1;
			if ('0' <= a.rs.str[0] && a.rs.str[0] <= '9')
				val = strtol(a.rs.str, NULL, 0);
			else 
				POS2LBL(a.rs.str);
			*(size_t *)(vbin + vbinlen) = val;
			vbinlen += sizeof val;
		shortop:
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
			// Fill in short jumps
			for (size_t j = 0; j < jmprelmapcount; j++) {
				const char *lbl = jmprelmap[j].lbl;
				if (streq(a.s.str, lbl)) {
					size_t pos = jmprelmap[j].pos;
					DEBUG("Filling in '%s'\t@ 0x%02lx (0x%02lx)", lbl, pos, vbinlen - pos);
					if (vbinlen - pos > 0x7F) {
						ERROR("Underestimated distance between label and relative"
						      " jump (%lu)", vbinlen - pos);
						EXIT(1);
					}
					vbin[pos] = vbinlen - pos;
					jmprelmapcount--;
					memmove(jmprelmap + j, jmprelmap + j + 1,
					        (jmprelmapcount - j) * sizeof *jmprelmap);
					j--;
				}
			}
			break;
		default:
			printf("IDK lol (%d)\n", a.op);
			abort();
		}
	}

	// Fill in local addresses
	// TODO

	*vbinlen_p = vbinlen;
	return 0;
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

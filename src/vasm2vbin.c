#include <assert.h>
#include <unistd.h>
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
	case ARGS_TYPE_NONE:
		return 1;
	case ARGS_TYPE_REG1:
		return 2;
	case ARGS_TYPE_REG2:
		return 3;
	case ARGS_TYPE_REG3:
		return 4;
	case ARGS_TYPE_BYTE:
		return 2;	
	case ARGS_TYPE_SHORT:
		return 3;
	case ARGS_TYPE_INT:
		return 5;	
	case ARGS_TYPE_LONG:
		return 9;
	case ARGS_TYPE_REGBYTE:
		return 3;	
	case ARGS_TYPE_REGSHORT:
		return 4;
	case ARGS_TYPE_REGINT:
		return 6;
	case ARGS_TYPE_REGLONG:
		return 10;
	case ARGS_TYPE_SPECIAL:
		switch (v.op) {
		case OP_RAW_BYTE:
			return 1;
		case OP_RAW_SHORT:
			return 2;
		case OP_RAW_INT:
			return 4;
		case OP_RAW_LONG:
			return 8;
		case OP_RAW_STR:
			; struct vasm_str *a = (struct vasm_str *)&v;
			return strlen(a->s);
		case OP_LABEL:
			return 0;
		default:
			EXIT(1, "Unknown op '%d'", v.op);
		}
	default:
		EXIT(1, "Unknown op '%d'", v.op);
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

		if (a.op == (unsigned char)OP_NONE  ||
		    a.op == (unsigned char)OP_COMMENT)
			continue;

		vbin[vbinlen] = vasms[i].op;
		vbinlen++;

		switch (a.op) {
		// No args
		case OP_SYSCALL:
		case OP_RET:
			break;
		// 1 reg
		case OP_PUSH:
		case OP_POP:
			vbin[vbinlen] = a.r.r;
			vbinlen++;
			break;
		// 2 reg
		case OP_STRL:
		case OP_STRI:
		case OP_STRS:
		case OP_STRB:
		case OP_LDL:
		case OP_LDI:
		case OP_LDS:
		case OP_LDB:
		case OP_MOV:
		case OP_NOT:
		case OP_INV:
			vbin[vbinlen++] = a.r2.r0;
			vbin[vbinlen++] = a.r2.r1;
			break;
		// 3 reg
		case OP_ADD:
		case OP_SUB:
		case OP_MUL:
		case OP_DIV:
		case OP_MOD:
		case OP_REM:
		case OP_RSHIFT:
		case OP_LSHIFT:
		case OP_AND:
		case OP_OR:
		case OP_XOR:
		case OP_STRLAT:
		case OP_STRIAT:
		case OP_STRSAT:
		case OP_STRBAT:
		case OP_LDLAT:
		case OP_LDIAT:
		case OP_LDSAT:
		case OP_LDBAT:
		case OP_LESS:
		case OP_LESSE:
			vbin[vbinlen++] = a.r3.r0;
			vbin[vbinlen++] = a.r3.r1;
			vbin[vbinlen++] = a.r3.r2;
			break;
		// 1 addr
		case OP_JMP:
			val = _getlblpos(a.s.s, map);
			if (val != -1) {
				ssize_t d = (ssize_t)val - (ssize_t)vbinlen;
				if (-0x80 <= d && d <= 0x7F) {
					vbin[vbinlen - 1] = OP_JMPRB;
					vbin[vbinlen++  ] = (char)d;
					break;
				}
			}
		case OP_CALL:
			val = -1;
			if (isnum(*a.s.s))
				val = strtol(a.s.s, NULL, 0);
			else 
				POS2LBL(a.s.s);
			*(size_t *)(vbin + vbinlen) = val;
			vbinlen += sizeof val;
			break;
		// 1 reg, 1 addr
		case OP_SET:
		case OP_SETB:
		case OP_SETS:
		case OP_SETI:
		case OP_SETL:
			vbin[vbinlen++] = a.rs.r;
			val = -1;
			if (isnum(*a.rs.s))
				val = strtol(a.rs.s, NULL, 0);
			else 
				POS2LBL(a.rs.s);
			if (a.op == OP_SET) {
#ifndef FORCE_SETL
				if (val <= 0xFF)
					a.op = OP_SETB;
				else if (val <= 0xFFFF)
					a.op = OP_SETS;
				else if (val <= 0xFFFFffff)
					a.op = OP_SETI;
				else if (val <= 0xFFFFffffFFFFffff)
					a.op = OP_SETL;
				else
					abort();
#else
				a.op = OP_SETL;
#endif
				vbin[vbinlen-2] = a.op;
			}
			switch (a.op) {
			case OP_SETB:
				if (val > 0xFF)
					abort();
				*(uint8_t *)(vbin + vbinlen) = val;
				vbinlen += 1;
				break;
			case OP_SETS:
				if (val > 0xFFFF)
					abort();
				*(uint16_t *)(vbin + vbinlen) = htobe16(val);
				vbinlen += 2;
				break;
			case OP_SETI:
				if (val > 0xFFFFffff)
					abort();
				*(uint32_t *)(vbin + vbinlen) = htobe32(val);
				vbinlen += 4;
				break;
			case OP_SETL:
				if (val > 0xFFFFffffFFFFffff)
					abort();
				*(uint64_t *)(vbin + vbinlen) = htobe64(val);
				vbinlen += 8;
				break;
			}
			break;
		// 1 reg, 1 addr
		case OP_JZ:
		case OP_JNZ:
		case OP_JP:
		case OP_JPZ:
			vbin[vbinlen] = a.rs.r;
			vbinlen++;
#if 1
			val = _getlblpos(a.rs.s, map);
			if (val != -1) {
				ssize_t d = (ssize_t)val - (ssize_t)vbinlen;
				if (-0x80 <= d && d <= 0x7F) {
					char op;
					switch (a.op) {
					case OP_JZ : op = OP_JZB ; break;
					case OP_JNZ: op = OP_JNZB; break;
					case OP_JP : op = OP_JPB ; break;
					case OP_JPZ: op = OP_JPZB; break;
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
					if (b.op == OP_LABEL &&
					    streq(a.rs.s, b.s.s)) {
						char op;
						switch (a.op) {
						case OP_JZ : op = OP_JZB ; break;
						case OP_JNZ: op = OP_JNZB; break;
						case OP_JP : op = OP_JPB ; break;
						case OP_JPZ: op = OP_JPZB; break;
						default: assert(0);
						}
						vbin[vbinlen - 2] = op;
						vbin[vbinlen++  ] = 0xFF;
						jmprelmap[jmprelmapcount].lbl = a.rs.s;
						jmprelmap[jmprelmapcount].pos = vbinlen - 1;
						jmprelmapcount++;
						goto shortop;
					}
				}
			}
#endif
			val = -1;
			if (isnum(*a.rs.s))
				val = strtol(a.rs.s, NULL, 0);
			else 
				POS2LBL(a.rs.s);
			*(size_t *)(vbin + vbinlen) = val;
			vbinlen += sizeof val;
		shortop:
			break;
		// Other
		case OP_RAW_LONG:
			vbinlen--;
			val = strtol(a.s.s, NULL, 0);
						*(unsigned long *)(vbin + vbinlen) = htobe64(val);
			vbinlen += sizeof (unsigned long);
			break;
		case OP_RAW_INT:
			vbinlen--;
			val = strtol(a.s.s, NULL, 0);
			*(unsigned int *)(vbin + vbinlen) = htobe32(val);
			vbinlen += sizeof (unsigned int);
			break;
		case OP_RAW_SHORT:
			vbinlen--;
			val = strtol(a.s.s, NULL, 0);
			*(unsigned short*)(vbin + vbinlen) = htobe16(val);
			vbinlen += sizeof (unsigned short);
			break;
		case OP_RAW_BYTE:
			vbinlen--;
			val = strtol(a.s.s, NULL, 0);
			*(unsigned char*)(vbin + vbinlen) = val;
			vbinlen += sizeof (unsigned char);
			break;
		case OP_RAW_STR:
			vbinlen--;
			val = strlen(a.s.s);
			for (size_t k = 0; k < val; k++) {
				char c = a.s.s[k];
				if (c == '\\') {
					k++;
					switch (a.s.s[k]) {
					case '\'':c = '\''; break;
					case '"': c = '"' ; break;
					case 'a': c = '\a'; break;
					case 'b': c = '\b'; break;
					case 'f': c = '\f'; break;
					case 'n': c = '\n'; break;
					case 'r': c = '\r'; break;
					case 't': c = '\t'; break;
					case 'v': c = '\v'; break;
					case 'x': EXIT(4, "Not implemented"); break;
					}
				}
				vbin[vbinlen++] = c;
			}
			break;
		case OP_LABEL:
			vbinlen--;
			map->lbl2pos[map->lbl2poscount].lbl = a.s.s;
			map->lbl2pos[map->lbl2poscount].pos = vbinlen;
			map->lbl2poscount++;
			// Fill in short jumps
			for (size_t j = 0; j < jmprelmapcount; j++) {
				const char *lbl = jmprelmap[j].lbl;
				if (streq(a.s.s, lbl)) {
					size_t pos = jmprelmap[j].pos;
					DEBUG("Filling in '%s'\t@ 0x%02lx (0x%02lx)", lbl, pos, vbinlen - pos);
					if (vbinlen - pos > 0x7F) {
						EXIT(1, "Underestimated distance between label and relative"
						        " jump (%lu)", vbinlen - pos);
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

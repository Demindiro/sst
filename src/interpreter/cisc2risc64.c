/**
 * This interpreter first converts all CISC instructions to RISC and executes these.
 *
 * The RISC instructions are 4 bytes long and have two formats:
 *
 *   - | 16 op | 5 rx | 5 ry | 5 rz | 1 |
 *   - | 16 op | 16 imm |
 *
 * The op is the lower 16 bits of a goto pointer.
 * r[xyz] are registers to (optionally) use.
 */


#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include <x86intrin.h>
#include "vasm.h"
#include "util.h"
#include "interpreter/syscall.h"


static char    mem[0x100000];
static int64_t regs[32];
static size_t  programlen;


#ifdef NDEBUG
# undef assert
# define assert(c, ...) if (!(c)) __builtin_unreachable()
#endif


#ifdef DEBUG
# undef DEBUG
#endif
#ifndef NDEBUG
# define DEBUG(m, ...) fprintf(stderr, m "\n", ##__VA_ARGS__)
#else
# define DEBUG(m, ...) NULL
#endif


#ifdef UNSAFE
# define _CHECKREG assert(regi < 32 && regj < 32 && regk < 32)
#else
# define _CHECKREG NULL
#endif
#ifdef PRECOMPUTE_REGI
# define REG1 NULL
#else
# define REG1 do {       regi = ((uint8_t *)ip)[-4]; } while (0)
#endif
#define REG2 do { REG1; regj = ((uint8_t *)ip)[-3]; } while (0)
#define REG3 do { REG2; regk = ((uint8_t *)ip)[-2]; } while (0)
#define REGI regs[regi]
#define REGJ regs[regj]
#define REGK regs[regk]

#ifdef NDEBUG
# define REG3OP(m,op) do {		\
		REG3;			\
		REGI = REGJ op REGK;	\
	} while (0)
# define REG3OPSTR(m,op,opstr) REG3OP(m,op)
# define REG3OPSTRFUNC(m,func,opstr,fmt) do {		\
	REG3;						\
	REGI = func(REGJ, REGK);			\
} while (0);
#else
# define REG3OP(m,op)							\
	do {								\
		REG3;							\
		size_t _v = REGJ, _w = REGK;				\
		REGI = REGJ op REGK;					\
		DEBUG(m "\tr%d,r%d,r%d\t(%lu = %lu " #op " %lu)",	\
		      regi, regj, regk, REGI, _v, _w);			\
	} while (0)
# define REG3OPSTR(m,op,opstr)						\
	do {								\
		REG3;							\
		size_t _v = REGJ, _w = REGK;				\
		REGI = REGJ op REGK;					\
		DEBUG(m "\tr%d,r%d,r%d\t(%lu = %lu " opstr " %lu)",	\
		      regi, regj, regk, REGI, _v, _w);			\
	} while (0)
# define REG3OPSTRFUNC(m,func,opstr,fmt) do {		\
	REG3;						\
	size_t _v = REGJ;				\
	REGI = func(REGJ, REGK);			\
	DEBUG(m "\tr%d,r%d,r%d\t"			\
	      "(" fmt " = " fmt " " opstr " " fmt ")",	\
	      regi, regj, regk, REGI, _v, REGK);	\
} while (0);
#endif

#define JUMPIF(m,c) do {				\
	REG1;						\
	if (c) {					\
		ip = (uint64_t *)*(uint64_t *)ip;	\
		DEBUG(m "\t0x%lx,r%d\t(%lu, true)",	\
		      (uint64_t)ip, regi, REGI);	\
	} else {					\
		DEBUG(m "\t0x%lx,r%d\t(%lu, false)",	\
		      (uint64_t)ip, regi, REGI);	\
		ip++;					\
	}						\
} while (0)

#define sp regs[31]


#ifndef NOPROF
static size_t icounter;
static size_t rstart;
#endif



#define RROT(t,x,y) (((t)x >> (t)y) | ((t)x << ((sizeof(t) * 8) - (t)y)))
#define LROT(t,x,y) (((t)x << (t)y) | ((t)x >> ((sizeof(t) * 8) - (t)y)))
#define RROT64(x,y) RROT(uint64_t,x,y)
#define LROT64(x,y) LROT(uint64_t,x,y)
#define RROT32(x,y) RROT(uint32_t,x,y)
#define LROT32(x,y) LROT(uint32_t,x,y)
#define RROT16(x,y) RROT(uint16_t,x,y)
#define LROT16(x,y) LROT(uint16_t,x,y)
#define RROT8(x,y)  RROT(uint8_t,x,y)
#define LROT8(x,y)  LROT(uint8_t,x,y)



enum risc_op {
	RISC_NOP,

	RISC_JMP,
	RISC_JZ,
	RISC_JNZ,
	RISC_JP,
	RISC_JPZ,
	RISC_CALL,
	RISC_RET,

	RISC_LDL,
	RISC_LDI,
	RISC_LDS,
	RISC_LDB,
	RISC_STRL,
	RISC_STRI,
	RISC_STRS,
	RISC_STRB,

	RISC_LDLAT,
	RISC_LDIAT,
	RISC_LDSAT,
	RISC_LDBAT,
	RISC_STRLAT,
	RISC_STRIAT,
	RISC_STRSAT,
	RISC_STRBAT,

	RISC_PUSH,
	RISC_POP,
	RISC_MOV,
	RISC_SETL,

	RISC_ADD,
	RISC_SUB,
	RISC_MUL,
	RISC_DIV,
	RISC_MOD,
	RISC_REM,
	RISC_LSHIFT,
	RISC_RSHIFT,
	RISC_LROT,
	RISC_RROT,
	RISC_AND,
	RISC_OR,
	RISC_XOR,
	RISC_NOT,
	RISC_INV,
	RISC_LESS,
	RISC_LESSE,

	RISC_SYSCALL,

	RISC_CRASH
};


static uint64_t risc[0x1000];


static enum risc_op cisc2risc_op(enum vasm_op cop)
{
	switch (cop) {
	case OP_NOP    : return RISC_NOP    ;
	case OP_JMPRB  :
	case OP_JMP    : return RISC_JMP    ;
	case OP_JZB    :
	case OP_JZ     : return RISC_JZ     ;
	case OP_JNZB   :
	case OP_JNZ    : return RISC_JNZ    ;
	case OP_JPB    :
	case OP_JP     : return RISC_JP     ;
	case OP_JPZB   :
	case OP_JPZ    : return RISC_JPZ    ;
	case OP_CALL   : return RISC_CALL   ;
	case OP_RET    : return RISC_RET    ;

	case OP_LDL    : return RISC_LDL    ;
	case OP_LDI    : return RISC_LDI    ;
	case OP_LDS    : return RISC_LDS    ;
	case OP_LDB    : return RISC_LDB    ;
	case OP_STRL   : return RISC_STRL   ;
	case OP_STRI   : return RISC_STRI   ;
	case OP_STRS   : return RISC_STRS   ;
	case OP_STRB   : return RISC_STRB   ;
	case OP_LDLAT  : return RISC_LDLAT  ;
	case OP_LDIAT  : return RISC_LDIAT  ;
	case OP_LDSAT  : return RISC_LDSAT  ;
	case OP_LDBAT  : return RISC_LDBAT  ;
	case OP_STRLAT : return RISC_STRLAT ;
	case OP_STRIAT : return RISC_STRIAT ;
	case OP_STRSAT : return RISC_STRSAT ;
	case OP_STRBAT : return RISC_STRBAT ;

	case OP_PUSH   : return RISC_PUSH   ;
	case OP_POP    : return RISC_POP    ;
	case OP_SETL   :
	case OP_SETI   :
	case OP_SETS   :
	case OP_SETB   : return RISC_SETL   ;
	case OP_MOV    : return RISC_MOV    ;

	case OP_ADD    : return RISC_ADD    ;
	case OP_SUB    : return RISC_SUB    ;
	case OP_MUL    : return RISC_MUL    ;
	case OP_DIV    : return RISC_DIV    ;
	case OP_MOD    : return RISC_MOD    ;
	case OP_REM    : return RISC_REM    ;
	case OP_AND    : return RISC_AND    ;
	case OP_OR     : return RISC_OR     ;
	case OP_XOR    : return RISC_XOR    ;
	case OP_LSHIFT : return RISC_LSHIFT ;
	case OP_RSHIFT : return RISC_RSHIFT ;
	case OP_RROT   : return RISC_RROT   ;
	case OP_LROT   : return RISC_LROT   ;
	case OP_NOT    : return RISC_NOT    ;
	case OP_INV    : return RISC_INV    ;
	case OP_LESS   : return RISC_LESS   ;
	case OP_LESSE  : return RISC_LESSE  ;

	case OP_SYSCALL: return RISC_SYSCALL;

	case OP_OP_LIMIT:
	case OP_NONE:
	case OP_COMMENT:
	case OP_LABEL:
	case OP_RAW:
	case OP_RAW_LONG:
	case OP_RAW_INT:
	case OP_RAW_SHORT:
	case OP_RAW_BYTE:
	case OP_RAW_STR:
	case OP_SET:
		break;
	}
	fprintf(stderr, "Invalid CISC op (%d)", cop);
	abort();
}


static void cisc2risc(void **tbl, size_t tbllen)
{
	// Make sure the prefix on all labels match
	DEBUG("Checking jump table prefixes");
	for (size_t i = 0; i < tbllen; i++) {
		size_t x = (size_t)tbl[0];
		size_t y = (size_t)tbl[i];
		DEBUG("  %lx  %04lx", (y & ~0xFFFFFFFFL) >> 16, y & 0xFFFFFFFFL);
		if ((x & ~0xFFFFFFFFL) != (y & ~0xFFFFFFFFL))
			abort();
#ifdef GUARANTEE_ZERO_PREFIX
		if ((x & ~0xFFFFFFFFL) != 0)
			abort();
#endif
	}
	DEBUG("Test passed");

	size_t n = 0, i = 0;


	struct {
		size_t cpos, rpos;
	} c2r[0x10000], r2c[0x1000];
	size_t c2rc = 0, r2cc = 0;

	
	while (i < programlen) {

		c2r[c2rc].cpos = i;
		c2r[c2rc].rpos = n;
		c2rc++;

		enum vasm_op op = mem[i++];
		uint64_t rx = 0, ry = 0, rz = 0;
		uint64_t val = 0;
		unsigned int vallen = 0;

		switch (get_vasm_args_type(op)) {
		case ARGS_TYPE_NONE:
			break;
		case ARGS_TYPE_REG1:
			rx = mem[i++];
			break;
		case ARGS_TYPE_REG2:
			rx = mem[i++];
			ry = mem[i++];
			break;
		case ARGS_TYPE_REG3:
			rx = mem[i++];
			ry = mem[i++];
			rz = mem[i++];
			break;
		case ARGS_TYPE_BYTE:
			vallen = 1;
			val    = (uint8_t)mem[i++];
			break;
		case ARGS_TYPE_SHORT:
			vallen = 2;
			val    = *(uint16_t *)(mem + i);
			val    = be16toh(val);
			i += 2;
			break;
		case ARGS_TYPE_INT:
			vallen = 4;
			val    = *(uint32_t *)(mem + i);
			val    = be32toh(val);
			i += 4;
			break;
		case ARGS_TYPE_LONG:
			vallen = 8;
			val    = *(uint64_t *)(mem + i);
			val    = be64toh(val);
			i += 8;
			break;
		case ARGS_TYPE_REGBYTE:
			rx = mem[i++];
			vallen = 1;
			val    = (uint8_t)mem[i++];
			break;
		case ARGS_TYPE_REGSHORT:
			rx = mem[i++];
			vallen = 2;
			val    = *(uint16_t *)(mem + i);
			val    = be16toh(val);
			i += 2;
			break;
		case ARGS_TYPE_REGINT:
			rx = mem[i++];
			vallen = 4;
			val    = *(uint32_t *)(mem + i);
			val    = be32toh(val);
			i += 4;
			break;
		case ARGS_TYPE_REGLONG:
			rx = mem[i++];
			vallen = 8;
			val    = *(uint64_t *)(mem + i);
			val    = be64toh(val);
			i += 8;
			break;
		default:
			DEBUG("Unknown OP @ %lx  (%d)", i - 1, op);
			op = RISC_CRASH;
			break;
		}

		uint64_t instr = 0;
		enum risc_op rop = cisc2risc_op(op);
		instr |= ((size_t)tbl[rop]) & 0x00000000FFFFFFFFL;

		instr |= (rx  << 32) & 0x000000FF00000000L;
		instr |= (ry  << 40) & 0x0000FF0000000000L;
		instr |= (rz  << 48) & 0x00FF000000000000L;

		risc[n++] = instr;

		if (op == OP_JMP ||
		    op == OP_JZ  ||
		    op == OP_JNZ ||
		    op == OP_JP  ||
		    op == OP_JPZ ||
		    op == OP_CALL) {
			r2c[r2cc].cpos = val;
			r2c[r2cc].rpos = n;
			r2cc++;
			val = -1;
			vallen = 8;
		}
		if (op == OP_JMPRB ||
		    op == OP_JZB   ||
		    op == OP_JNZB  ||
		    op == OP_JPB   ||
		    op == OP_JPZB ) {
			r2c[r2cc].cpos = i - 1 + (int8_t)val;
			r2c[r2cc].rpos = n;
			r2cc++;
			val = -1;
			vallen = 8;
		}

		if (vallen > 0) {
			risc[n++] = val;
		}
	}


	for (size_t i = 0; i < r2cc; i++) {
		size_t j;
		for (j = 0; j < c2rc; j++) {
			if (r2c[i].cpos == c2r[j].cpos)
				goto found;
		}
		abort();
	found:
		*(uint64_t *)(risc + r2c[i].rpos) = (uint64_t)(risc + c2r[j].rpos);
	}
}



#pragma GCC push_options
#pragma GCC optimize ("align-functions=16")

//__attribute__((section(".loop")))
static void run()
{
#ifndef NOPROF
	rstart = _rdtsc();
#endif

	static void *table[] = {
		[RISC_NOP]     = &&op_nop,

		[RISC_JMP]     = &&op_jmp,
		[RISC_JZ]      = &&op_jz,
		[RISC_JNZ]     = &&op_jnz,
		[RISC_JP]      = &&op_jp,
		[RISC_JPZ]     = &&op_jpz,
		[RISC_CALL]    = &&op_call,
		[RISC_RET]     = &&op_ret,

		[RISC_LDL]     = &&op_ldl,
		[RISC_LDI]     = &&op_ldi,
		[RISC_LDS]     = &&op_lds,
		[RISC_LDB]     = &&op_ldb,
		[RISC_STRL]    = &&op_strl,
		[RISC_STRI]    = &&op_stri,
		[RISC_STRS]    = &&op_strs,
		[RISC_STRB]    = &&op_strb,

		[RISC_LDLAT]   = &&op_ldlat,
		[RISC_LDIAT]   = &&op_ldiat,
		[RISC_LDSAT]   = &&op_ldsat,
		[RISC_LDBAT]   = &&op_ldbat,
		[RISC_STRLAT]  = &&op_strlat,
		[RISC_STRIAT]  = &&op_striat,
		[RISC_STRSAT]  = &&op_strsat,
		[RISC_STRBAT]  = &&op_strbat,

		[RISC_PUSH]    = &&op_push,
		[RISC_POP]     = &&op_pop,
		[RISC_MOV]     = &&op_mov,
		[RISC_SETL]    = &&op_setl,

		[RISC_ADD]     = &&op_add,
		[RISC_SUB]     = &&op_sub,
		[RISC_MUL]     = &&op_mul,
		[RISC_DIV]     = &&op_div,
		[RISC_MOD]     = &&op_mod,
		[RISC_REM]     = &&op_rem,
		[RISC_LSHIFT]  = &&op_lshift,
		[RISC_RSHIFT]  = &&op_rshift,
		[RISC_LROT]    = &&op_lrot,
		[RISC_RROT]    = &&op_rrot,
		[RISC_AND]     = &&op_and,
		[RISC_OR]      = &&op_or,
		[RISC_XOR]     = &&op_xor,
		[RISC_NOT]     = &&op_not,
		[RISC_INV]     = &&op_inv,
		[RISC_LESS]    = &&op_less,
		[RISC_LESSE]   = &&op_lesse,

		[RISC_SYSCALL] = &&op_syscall,

		[RISC_CRASH]   = &&crash,
	};

	cisc2risc(table, sizeof table / sizeof *table);

	uint64_t *ip = risc;

#ifdef GUARANTEE_ZERO_PREFIX
#define prefix 0
#else
	size_t   prefix = ((size_t)table[0]) & ~0xFFFFFFFFL;
#endif

	while (1) {
#ifndef NOPROF
		icounter++;
#endif
#ifndef NDEBUG
		fprintf(stderr, "0x%06lx:\t", (uint64_t)ip);
#endif
#ifdef THROTTLE
		usleep(THROTTLE * 1000);
#endif
		uint64_t      instr = *ip++;
#ifndef NDEBUG
		fprintf(stderr, "%016lx\t", instr);
#endif
		size_t        addr  = ((instr      ) & 0xFFFFFFFFL) + prefix;
		unsigned char regi, regj, regk;
#ifdef PRECOMPUTE_REGI
# define PREREGI regi = ((uint8_t *)ip)[-4];
#else
# define PREREGI NULL
#endif
		PREREGI;

		goto *(void *)addr;
#if defined(NDEBUG) && 0
#define continue					\
		instr = *ip++;				\
		addr = (instr & 0xFFFFFFFFL) + prefix;	\
		PREREGI;				\
		goto *(void *)addr;
#endif

	op_nop:
		DEBUG("nop");
		continue;

	op_call:
		*(uint64_t *)(mem + sp) = (uint64_t)(ip + 1);
		sp += sizeof ip;
		ip = (uint64_t *)*(uint64_t *)ip;
		DEBUG("call\t0x%lx", (uint64_t)ip);
		continue;

	op_ret:
		sp -= sizeof ip;
		ip = (uint64_t *)*(uint64_t *)(mem + sp);
		DEBUG("ret\t\t(0x%lx)", (uint64_t)ip);
		continue;

	op_jmp:
		ip = (uint64_t *)*(uint64_t *)ip;
		DEBUG("jmp\t0x%lx", (uint64_t)ip);
		continue;

	op_jz:
		JUMPIF("jz", !REGI);
		continue;

	op_jnz:
		JUMPIF("jnz", REGI);
		continue;

	op_jp:
		JUMPIF("jp", REGI > 0);
		continue;

	op_jpz:
		JUMPIF("jpz", REGI >= 0);
		continue;

	op_ldl:
		REG2;
		REGI = *(uint64_t *)(mem + REGJ);
		REGI = be64toh(REGI);
		DEBUG("ldl\tr%d,r%d\t(%lu <-- 0x%lx)", regi, regj, REGI, REGJ);
		continue;

	op_ldi:
		REG2;
		REGI = *(uint32_t *)(mem + REGJ);
		REGI = be32toh(REGI);
		DEBUG("ldl\tr%d,r%d\t(%lu <-- 0x%lx)", regi, regj, REGI, REGJ);
		continue;

	op_lds:
		REG2;
		REGI = *(uint16_t *)(mem + REGJ);
		REGI = be16toh(REGI);
		DEBUG("ldl\tr%d,r%d\t(%lu <-- 0x%lx)", regi, regj, REGI, REGJ);
		continue;

	op_ldb:
		REG2;
		REGI = *(uint8_t *)(mem + REGJ);
		DEBUG("ldl\tr%d,r%d\t(%lu <-- 0x%lx)", regi, regj, REGI, REGJ);
		continue;

	op_ldlat:
		REG3;
		REGI = *(uint64_t *)(mem + REGJ + REGK);
		REGI = be64toh(REGI);
		DEBUG("ldlat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)",
		      regi, regj, regk, REGI, REGJ, REGK);
		continue;

	op_ldiat:
		REG3;
		REGI = *(uint32_t *)(mem + REGJ + REGK);
		REGI = be32toh(REGI);
		DEBUG("ldiat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)",
		      regi, regj, regk, REGI, REGJ, REGK);
		continue;

	op_ldsat:
		REG3;
		REGI = *(uint16_t *)(mem + REGJ + REGK);
		REGI = be16toh(REGI);
		DEBUG("ldsat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)",
		      regi, regj, regk, REGI, REGJ, REGK);
		continue;

	op_ldbat:
		REG3;
		REGI = *(uint8_t *)(mem + REGJ + REGK);
		DEBUG("ldbat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)",
		      regi, regj, regk, REGI, REGJ, REGK);
		continue;

	op_strl:
		REG2;
		*(uint64_t *)(mem + REGJ) = htobe64(REGI);
		DEBUG("strl\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
		continue;

	op_stri:
		REG2;
		*(uint32_t *)(mem + REGJ) = htobe32(REGI);
		DEBUG("stri\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
		continue;

	op_strs:
		REG2;
		*(uint16_t *)(mem + REGJ) = htobe16(REGI);
		DEBUG("strs\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
		continue;

	op_strb:
		REG2;
		*(uint8_t *)(mem + REGJ) = REGI;
		DEBUG("strb\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
		continue;

	op_strlat:
		REG3;
		*(uint64_t *)(mem + REGJ + REGK) = htobe64(REGI);
		DEBUG("strlat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)",
		      regi, regj, regk, REGI, REGJ, REGK);
		continue;

	op_striat:
		REG3;
		*(uint32_t *)(mem + REGJ + REGK) = htobe32(REGI);
		DEBUG("striat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)",
		      regi, regj, regk, REGI, REGJ, REGK);
		continue;

	op_strsat:
		REG3;
		*(uint16_t *)(mem + REGJ + REGK) = htobe16(REGI);
		DEBUG("strsat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)",
		      regi, regj, regk, REGI, REGJ, REGK);
		continue;

	op_strbat:
		REG3;
		*(uint8_t *)(mem + REGJ + REGK) = REGI;
		DEBUG("strbat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)",
		      regi, regj, regk, REGI, REGJ, REGK);
		continue;

	op_push:
		REG1;
		*(size_t *)(mem + sp) = REGI;
		DEBUG("push\tr%d\t(%lu)", regi, *(size_t *)(mem + sp));
		sp += sizeof REGI;
		continue;

	op_pop:
		REG1;
		sp -= sizeof REGI;
		REGI = *(size_t *)(mem + sp);
		DEBUG("pop\tr%d\t(%ld)", regi, REGI);
		continue;

	op_mov:
		REG2;
		REGI = REGJ;
		DEBUG("mov\tr%d,r%d\t(%lu)", regi, regj, REGI);
		continue;

	op_setl:
		REG1;
		REGI = *ip++;
		DEBUG("setl\tr%d,%lu\t(%lx)", regi, REGI, REGI);
		continue;

	op_add:
		REG3OP("add", +);
		continue;

	op_sub:
		REG3OP("sub", -);
		continue;

	op_mul:
		REG3OP("mul", *);
		continue;

	op_div:
		REG3OP("div", /);
		continue;

	op_mod:
		REG3OPSTR("mod", %, "%%");
		continue;

	op_rem:
		REG3OPSTR("rem", %, "%%");
		continue;

	op_and:
		REG3OP("and", &);
		continue;

	op_or:
		REG3OP("or", |);
		continue;

	op_xor:
		REG3OP("xor", ^);
		continue;

	op_lshift:
		REG3OP("lshift", <<);
		continue;

	op_rshift:
		REG3OP("rshift", >>);
		continue;

	op_less:
		REG3OP("less", <);
		continue;

	op_lesse:
		REG3OP("lesse", <=);
		continue;

	op_rrot:
		REG3OPSTRFUNC("rrot", RROT64, "RR", "%lx");
		continue;

	op_lrot:
		REG3OPSTRFUNC("rrot", LROT64, "RR", "%lx");
		continue;

	op_not:
		REG2;
		REGI = ~REGJ;
		DEBUG("not\tr%d,r%d\t(%lu)", regi, regj, REGI);
		continue;

	op_inv:
		REG2;
		REGI = !REGJ;
		DEBUG("inv\tr%d,r%d\t(%lu)", regi, regj, REGI);
		continue;

	op_syscall:
		DEBUG("syscall");
		vasm_syscall(regs, mem);
		continue;

	crash:
		fprintf(stderr, "Invalid OP executed");
		fprintf(stderr, "Crashing");
		exit(1);
	}
}

#pragma GCC pop_options


int main(int argc, char **argv) {
	
	// Read source
	int fd = open(argv[1], O_RDONLY);
	int magic;
	read(fd, &magic, sizeof magic);
	programlen = read(fd, mem, sizeof mem);
	close(fd);

	// Magic
	run();
}

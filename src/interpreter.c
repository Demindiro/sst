/**
 */

#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include <x86intrin.h>
#include "vasm.h"


static char    mem[0x100000];
static int64_t regs[32];
static int64_t ip;


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
#define REG1 do { regi = mem[ip++]; _CHECKREG; } while (0)
#define REG2 do { regi = mem[ip++]; regj = mem[ip++]; _CHECKREG; } while (0)
#define REG3 do { regi = mem[ip++]; regj = mem[ip++]; regk = mem[ip++]; _CHECKREG; } while (0)
#define REGI regs[regi]
#define REGJ regs[regj]
#define REGK regs[regk]

#ifdef NDEBUG
# define REG3OP(m,op) \
	do { \
		REG3; \
		REGI = REGJ op REGK; \
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
		ip = *(size_t *)(mem + ip);		\
		ip = be64toh(ip);			\
		DEBUG(m "\t0x%lx,r%d\t(%lu, true)",	\
		      ip, regi, REGI);			\
	} else {					\
		DEBUG(m "\t0x%lx,r%d\t(%lu, false)",	\
		      ip, regi, REGI);			\
		ip += sizeof ip;			\
	}						\
} while (0)

#define JUMPRELIF(m,c,t,conv) do {				\
	REG1;							\
	if (c) {						\
		t v = conv((t)mem[ip]);				\
		ip += v;					\
		DEBUG(m "\t%s0x%x,r%d\t(%lu, true, 0x%lx)",	\
		      v < 0 ? "-" : "", v < 0 ? -v : v,		\
		      regi, REGI, ip);				\
	} else {						\
		t v = conv((t)mem[ip]);				\
		DEBUG(m "\t%s0x%x,r%d\t(%lu, false, 0x%lx)",	\
		      v < 0 ? "-" : "", v < 0 ? -v : v,		\
		      regi, REGI, ip);				\
		ip += sizeof (t);				\
	}							\
} while (0)


#define sp regs[31]


#ifndef NOPROF
static size_t icounter;
static size_t rstart;
#endif



// Would you believe me if I said not forcing no inlining would cause the
// interpreter to be 3 times slower for _every_ instruction? (~2 seconds
// vs 0.68 seconds)
//
// My theory as to what is happening: GCC inlines this function, the big
// switch statement in the main loop gets a case that is too big for inlining
// and no jump table is created, which results in tons of branches and
// lots of branch misses.
//
// Evidence for suspicion: 163 905 110 branch misses without noinline,
// 53 776 with (x3000 difference).
//
// Maybe I should add this bit of knowledge to Stack Overflow. Surely there are others
// unknowingly leaving performance on the table due to this :P
// Or maybe a bug report would be more appropriate. Oh well.
__attribute__((noinline))
static void vasm_syscall() {
	switch (regs[0]) {
	case 0: // exit(code)
#ifndef NOPROF
		; size_t r = _rdtsc() - rstart;
		printf("\n");
		printf("Instructions executed: %lu\n", icounter);
		printf("Host CPU cycles: %lu\n", r);
		printf("Average host CPU cycles per instruction: %lf\n", (double)r / icounter);
#endif
		exit(regs[1]);
		break;
	case 1: // read(fd, buf, length)
		regs[0] = write(regs[1], (char *)(mem + regs[2]), regs[3]);
		DEBUG("write(%lu, 0x%lx, %lu) = %ld",
		        regs[1], regs[2], regs[3], regs[0]);
		break;
	case 2: // write(fd, buf, length)
		regs[0] = read(regs[1], (char *)(mem + regs[2]), regs[3]);
		DEBUG("read(%lu, 0x%lx, %lu) = %ld",
		        regs[1], regs[2], regs[3], regs[0]);
		break;
	case 3: // connect(ip6, port)
	case 4: // listen(ip6, port)
	case 5: // accept(fd)
	default:
		regs[0] = -1;
		break;
	}
}



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



static void run() {

#ifndef NOPROF
	rstart = _rdtsc();
#endif

	static void *table[] = {
		[OP_NOP] = &&op_nop,

		[OP_JMP] = &&op_jmp,
		[OP_JZ] = &&op_jz,
		[OP_JNZ] = &&op_jnz,
		[OP_JP] = &&op_jp,
		[OP_JPZ] = &&op_jpz,
		[OP_CALL] = &&op_call,
		[OP_RET] = &&op_ret,
		[OP_JMPRB] = &&op_jmprb,
		[OP_JZB] = &&op_jzb,
		[OP_JNZB] = &&op_jnzb,
		[OP_JPB] = &&op_jpb,
		[OP_JPZB] = &&op_jpzb,

		[OP_LDL] = &&op_ldl,
		[OP_LDI] = &&op_ldi,
		[OP_LDS] = &&op_lds,
		[OP_LDB] = &&op_ldb,
		[OP_STRL] = &&op_strl,
		[OP_STRI] = &&op_stri,
		[OP_STRS] = &&op_strs,
		[OP_STRB] = &&op_strb,

		[OP_LDLAT] = &&op_ldlat,
		[OP_LDIAT] = &&op_ldiat,
		[OP_LDSAT] = &&op_ldsat,
		[OP_LDBAT] = &&op_ldbat,
		[OP_STRLAT] = &&op_strlat,
		[OP_STRIAT] = &&op_striat,
		[OP_STRSAT] = &&op_strsat,
		[OP_STRBAT] = &&op_strbat,

		[OP_PUSH] = &&op_push,
		[OP_POP] = &&op_pop,
		[OP_MOV] = &&op_mov,
		[OP_SETL] = &&op_setl,
		[OP_SETI] = &&op_seti,
		[OP_SETS] = &&op_sets,
		[OP_SETB] = &&op_setb,

		[OP_ADD] = &&op_add,
		[OP_SUB] = &&op_sub,
		[OP_MUL] = &&op_mul,
		[OP_DIV] = &&op_div,
		[OP_MOD] = &&op_mod,
		[OP_REM] = &&op_rem,
		[OP_LSHIFT] = &&op_lshift,
		[OP_RSHIFT] = &&op_rshift,
		[OP_LROT] = &&op_lrot,
		[OP_RROT] = &&op_rrot,
		[OP_AND] = &&op_and,
		[OP_OR] = &&op_or,
		[OP_XOR] = &&op_xor,
		[OP_NOT] = &&op_not,
		[OP_INV] = &&op_inv,
		[OP_LESS] = &&op_less,
		[OP_LESSE] = &&op_lesse,

		[OP_SYSCALL] = &&op_syscall,
	};


	while (1) {
#ifndef NOPROF
		icounter++;
#endif
#ifndef NDEBUG
		fprintf(stderr, "0x%06lx:\t", ip);
#endif
#ifdef THROTTLE
		usleep(THROTTLE * 1000);
#endif
		enum vasm_op op = mem[ip];
		ip++;
		goto *table[op];

		unsigned char regi, regj, regk;
		size_t addr, val;

	op_nop:
		DEBUG("nop");
		continue;

	op_call:
		addr = htobe64(ip + sizeof ip);
		*(size_t *)(mem + sp) = addr;
		sp += sizeof ip;
		ip = *(size_t *)(mem + ip);
		ip = be64toh(ip);
		DEBUG("call\t0x%lx", ip);
		continue;

	op_ret:
		sp -= sizeof ip;
		ip = *(size_t *)(mem + sp);
		ip = htobe64(ip);
		DEBUG("ret\t\t(0x%lx)", ip);
		continue;

	op_jmp:
		ip = *(size_t *)(mem + ip);
		ip = be64toh(ip);
		DEBUG("jmp\t0x%lx", ip);
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

	op_jmprb:
#ifndef NDEBUG
		{
			int8_t c = mem[ip];
			if (c >= 0)
				DEBUG("jmprb\t0x%x\t(0x%lx)",
				      (uint8_t)c, ip + c);
			else
				DEBUG("jmprb\t0x%x\t(0x%lx)",
				      (uint8_t)c, ip + c);
			ip += c;
		}
#else
		ip += (int8_t)mem[ip];
#endif
		continue;

	op_jzb:
		JUMPRELIF("jzb", !REGI, int8_t, (int8_t));
		continue;

	op_jnzb:
		JUMPRELIF("jnzb", REGI, int8_t, (int8_t));
		continue;

	op_jpb:
		JUMPRELIF("jpb", REGI > 0, int8_t, (int8_t));
		continue;

	op_jpzb:
		JUMPRELIF("jpzb", REGI >= 0, int8_t, (int8_t));
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
		sp += sizeof regs[regi];
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
		val = *(uint64_t *)(mem + ip);
		val = be64toh(val);
		ip += 8;
		REGI = val;
		DEBUG("setl\tr%d,%lu\t(%lu)", regi, val, REGI);
		continue;

	op_seti:
		REG1;
		val = *(uint32_t *)(mem + ip);
		val = be32toh(val);
		ip += 4;
		REGI = val;
		DEBUG("seti\tr%d,%lu\t(%lu)", regi, val, REGI);
		continue;

	op_sets:
		REG1;
		val = *(uint16_t *)(mem + ip);
		val = be16toh(val);
		ip += 2;
		REGI = val;
		DEBUG("sets\tr%d,%lu\t(%lu)", regi, val, REGI);
		continue;

	op_setb:
		REG1;
		val = *(uint8_t *)(mem + ip);
		ip += 1;
		REGI = val;
		DEBUG("setb\tr%d,%lu\t(%lu)", regi, val, REGI);
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
		vasm_syscall();
		continue;
	}
}



int main(int argc, char **argv) {
	
	// Read source
	int fd = open(argv[1], O_RDONLY);
	int magic;
	read(fd, &magic, sizeof magic);
	read(fd, mem, sizeof mem);
	close(fd);

	// Magic
	run();
}

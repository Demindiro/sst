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
# define REG3OP(m,op) \
	do {\
		REG3; \
		size_t _v = REGJ; \
		REGI = REGJ op REGK; \
		DEBUG(m "\tr%d,r%d,r%d\t(%lu = %lu " #op " %lu)", \
		      regi, regj, regk, REGI, _v, REGK); \
	} while (0)
# define REG3OPSTR(m,op,opstr) \
	do {\
		REG3; \
		size_t _v = REGJ; \
		REGI = REGJ op REGK; \
		DEBUG(m "\tr%d,r%d,r%d\t(%lu = %lu " opstr " %lu)", \
		      regi, regj, regk, REGI, _v, REGK); \
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
	case 0:
#ifndef NOPROF
		; size_t r = _rdtsc() - rstart;
		printf("\n");
		printf("Instructions executed: %lu\n", icounter);
		printf("Host CPU cycles: %lu\n", r);
		printf("Average host CPU cycles per instruction: %lf\n", (double)r / icounter);
#endif
		exit(regs[1]);
		break;
	case 1:
		regs[0] = write(regs[1], (char *)(mem + regs[2]), regs[3]);
		DEBUG("write(%lu, 0x%lx, %lu) = %ld",
		        regs[1], regs[2], regs[3], regs[0]);
		break;
	case 2:
		regs[0] = read(regs[1], (char *)(mem + regs[2]), regs[3]);
		DEBUG("read(%lu, 0x%lx, %lu) = %ld",
		        regs[1], regs[2], regs[3], regs[0]);
		break;
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

	while (1) {
#ifndef NOPROF
		icounter++;
#endif
#ifndef NDEBUG
		fprintf(stderr, "0x%06lx:\t", ip);
#endif
		enum vasm_op op = mem[ip];
		ip++;

		unsigned char regi, regj, regk;
		size_t addr, val;

		switch (op) {
		case OP_NOP:
			DEBUG("nop");
			break;
		case OP_CALL:
			addr = htobe64(ip + sizeof ip);
			*(size_t *)(mem + sp) = addr;
			sp += sizeof ip;
			ip = *(size_t *)(mem + ip);
			ip = be64toh(ip);
			DEBUG("call\t0x%lx", ip);
			break;
		case OP_RET: 
			sp -= sizeof ip;
			ip = *(size_t *)(mem + sp);
			ip = htobe64(ip);
			DEBUG("ret\t\t(0x%lx)", ip);
			break;
		case OP_JMP:
			ip = *(size_t *)(mem + ip);
			ip = be64toh(ip);
			DEBUG("jmp\t0x%lx", ip);
			break;
		case OP_JZ:
			JUMPIF("jz", !REGI);
			break;
		case OP_JNZ:
			JUMPIF("jnz", REGI);
			break;
		case OP_JP:
			JUMPIF("jp", REGI > 0);
			break;
		case OP_JPZ:
			JUMPIF("jpz", REGI >= 0);
			break;
		case OP_JMPRB:
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
			break;
		case OP_JZB:
			JUMPRELIF("jzb", !REGI, int8_t, (int8_t));
			break;
		case OP_JNZB:
			JUMPRELIF("jnzb", REGI, int8_t, (int8_t));
			break;
		case OP_JPB:
			JUMPRELIF("jpb", REGI > 0, int8_t, (int8_t));
			break;
		case OP_JPZB:
			JUMPRELIF("jpzb", REGI >= 0, int8_t, (int8_t));
			break;
		case OP_LDL:
			REG2;
			REGI = *(uint64_t *)(mem + REGJ);
			REGI = be64toh(REGI);
			DEBUG("ldl\tr%d,r%d\t(%lu <-- 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case OP_LDI:
			REG2;
			REGI = *(uint32_t *)(mem + REGJ);
			REGI = be32toh(REGI);
			DEBUG("ldl\tr%d,r%d\t(%lu <-- 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case OP_LDS:
			REG2;
			REGI = *(uint16_t *)(mem + REGJ);
			REGI = be16toh(REGI);
			DEBUG("ldl\tr%d,r%d\t(%lu <-- 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case OP_LDB:
			REG2;
			REGI = *(uint8_t *)(mem + REGJ);
			DEBUG("ldl\tr%d,r%d\t(%lu <-- 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case OP_LDLAT:
			REG3;
			REGI = *(uint64_t *)(mem + REGJ + REGK);
			REGI = be64toh(REGI);
			DEBUG("ldlat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)",
			      regi, regj, regk, REGI, REGJ, REGK);
			break;
		case OP_LDIAT:
			REG3;
			REGI = *(uint32_t *)(mem + REGJ + REGK);
			REGI = be32toh(REGI);
			DEBUG("ldiat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)",
			      regi, regj, regk, REGI, REGJ, REGK);
			break;
		case OP_LDSAT:
			REG3;
			REGI = *(uint16_t *)(mem + REGJ + REGK);
			REGI = be16toh(REGI);
			DEBUG("ldsat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)",
			      regi, regj, regk, REGI, REGJ, REGK);
			break;
		case OP_LDBAT:
			REG3;
			REGI = *(uint8_t *)(mem + REGJ + REGK);
			DEBUG("ldbat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)",
			      regi, regj, regk, REGI, REGJ, REGK);
			break;
		case OP_STRL:
			REG2;
			*(uint64_t *)(mem + REGJ) = htobe64(REGI);
			DEBUG("strl\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case OP_STRI:
			REG2;
			*(uint32_t *)(mem + REGJ) = htobe32(REGI);
			DEBUG("stri\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case OP_STRS:
			REG2;
			*(uint16_t *)(mem + REGJ) = htobe16(REGI);
			DEBUG("strs\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case OP_STRB:
			REG2;
			*(uint8_t *)(mem + REGJ) = REGI;
			DEBUG("strb\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case OP_STRLAT:
			REG3;
			*(uint64_t *)(mem + REGJ + REGK) = htobe64(REGI);
			DEBUG("strlat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)",
			      regi, regj, regk, REGI, REGJ, REGK);
			break;
		case OP_STRIAT:
			REG3;
			*(uint32_t *)(mem + REGJ + REGK) = htobe32(REGI);
			DEBUG("striat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)",
			      regi, regj, regk, REGI, REGJ, REGK);
			break;
		case OP_STRSAT:
			REG3;
			*(uint16_t *)(mem + REGJ + REGK) = htobe16(REGI);
			DEBUG("strsat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)",
			      regi, regj, regk, REGI, REGJ, REGK);
			break;
		case OP_STRBAT:
			REG3;
			*(uint8_t *)(mem + REGJ + REGK) = REGI;
			DEBUG("strbat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)",
			      regi, regj, regk, REGI, REGJ, REGK);
			break;
		case OP_PUSH:
			REG1;
			*(size_t *)(mem + sp) = REGI;
			DEBUG("push\tr%d\t(%lu)", regi, *(size_t *)(mem + sp));
			sp += sizeof regs[regi];
			break;
		case OP_POP:
			REG1;
			sp -= sizeof REGI;
			REGI = *(size_t *)(mem + sp);
			DEBUG("pop\tr%d\t(%ld)", regi, REGI);
			break;
		case OP_MOV:
			REG2;
			REGI = REGJ;
			DEBUG("mov\tr%d,r%d\t(%lu)", regi, regj, REGI);
			break;
		case OP_SETL:
			REG1;
			val = *(uint64_t *)(mem + ip);
			val = be64toh(val);
			ip += 8;
			REGI = val;
			DEBUG("setl\tr%d,%lu\t(%lu)", regi, val, REGI);
			break;
		case OP_SETI:
			REG1;
			val = *(uint32_t *)(mem + ip);
			val = be32toh(val);
			ip += 4;
			REGI = val;
			DEBUG("seti\tr%d,%lu\t(%lu)", regi, val, REGI);
			break;
		case OP_SETS:
			REG1;
			val = *(uint16_t *)(mem + ip);
			val = be16toh(val);
			ip += 2;
			REGI = val;
			DEBUG("sets\tr%d,%lu\t(%lu)", regi, val, REGI);
			break;
		case OP_SETB:
			REG1;
			val = *(uint8_t *)(mem + ip);
			ip += 1;
			REGI = val;
			DEBUG("setb\tr%d,%lu\t(%lu)", regi, val, REGI);
			break;
		case OP_ADD:
			REG3OP("add", +);
			break;
		case OP_SUB:
			REG3OP("sub", -);
			break;
		case OP_MUL:
			REG3OP("mul", *);
			break;
		case OP_DIV:
			REG3OP("div", /);
			break;
		case OP_MOD:
			REG3OPSTR("mod", %, "%%");
			break;
		case OP_REM:
			REG3OPSTR("rem", %, "%%");
			break;
		case OP_AND:
			REG3OP("and", &);
			break;
		case OP_OR:
			REG3OP("or", |);
			break;
		case OP_XOR:
			REG3OP("xor", ^);
			break;
		case OP_LSHIFT:
			REG3OP("lshift", <<);
			break;
		case OP_RSHIFT:
			REG3OP("rshift", >>);
			break;
		case OP_LESS:
			REG3OP("less", <);
			break;
		case OP_LESSE:
			REG3OP("lesse", <=);
			break;
		case OP_RROT:
			REG3OPSTRFUNC("rrot", RROT64, "RR", "%lx");
			break;
		case OP_LROT:
			REG3OPSTRFUNC("rrot", LROT64, "RR", "%lx");
			break;
		case OP_NOT:
			REG2;
			REGI = ~REGJ;
			DEBUG("not\tr%d,r%d\t(%lu)", regi, regj, REGI);
			break;
		case OP_INV:
			REG2;
			REGI = !REGJ;
			DEBUG("inv\tr%d,r%d\t(%lu)", regi, regj, REGI);
			break;
		case OP_SYSCALL:
			DEBUG("syscall");
			vasm_syscall();
			break;
		case OP_OP_LIMIT:
		case OP_NONE:
		case OP_SET:
		case OP_LABEL:
		case OP_COMMENT:
		case OP_RAW:
		case OP_RAW_LONG:
		case OP_RAW_INT:
		case OP_RAW_SHORT:
		case OP_RAW_BYTE:
		case OP_RAW_STR:
#ifdef NDEBUG
		default:
#endif
			assert(0);
		}
#ifdef THROTTLE
		usleep(THROTTLE * 1000);
#endif
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

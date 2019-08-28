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


static char   mem[0x100000];
static long   regs[32];
static size_t ip;


#ifdef NDEBUG
# undef assert
# define assert(c, ...) if (c) __builtin_unreachable()
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
#endif

#ifndef NDEBUG
# define DEBUG(x, ...) fprintf(stderr, x "\n", ##__VA_ARGS__)
#else
# define DEBUG(x, ...) do {} while (0)
#endif

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


static void run() {

	sp = 0x10000;

#ifndef NOPROF
	rstart = _rdtsc();
#endif

	while (1) {
#ifndef NOPROF
		icounter++;
#endif
#ifndef NDEBUG
		fprintf(stderr, "0x%08lx: ", ip);
#endif
		char op = mem[ip];
		ip++;

		unsigned char regi, regj, regk;
		size_t addr, val;

		switch (op) {
		case VASM_OP_NOP:
			DEBUG("nop");
			break;
		case VASM_OP_CALL:
			addr = htobe64(ip + sizeof ip);
			*(size_t *)(mem + sp) = addr;
			sp += sizeof ip;
			ip = *(size_t *)(mem + ip);
			ip = be64toh(ip);
			DEBUG("call\t0x%lx", ip);
			break;
		case VASM_OP_RET: 
			sp -= sizeof ip;
			ip = *(size_t *)(mem + sp);
			ip = htobe64(ip);
			DEBUG("ret\t\t(0x%lx)", ip);
			break;
		case VASM_OP_JMP:
			ip = *(size_t *)(mem + ip);
			ip = be64toh(ip);
			DEBUG("jmp\t0x%lx", ip);
			break;
		case VASM_OP_JZ:
			REG1;
			if (!REGI) {
				ip = *(size_t *)(mem + ip);
				ip = be64toh(ip);
				DEBUG("jz\t0x%lx,r%d\t(%lu, true)",
				      ip, regi, REGI);
			} else {
				DEBUG("jz\t0x%lx,r%d\t(%lu, false)",
				      ip, regi, REGI);
				ip += sizeof ip;
			}
			break;
		case VASM_OP_JNZ:
			REG1;
			if (REGI) {
				ip = *(size_t *)(mem + ip);
				ip = be64toh(ip);
				DEBUG("jnz\t0x%lx,r%d\t(%lu, true)",
				      ip, regi, REGI);
			} else {
				DEBUG("jnz\t0x%lx,r%d\t(%lu, false)",
				      ip, regi, REGI);
				ip += sizeof ip;
			}
			break;
		case VASM_OP_JMPRB:
#ifndef NDEBUG
			{
				int8_t c = mem[ip];
				if (c >= 0)
					DEBUG("jmprb\t0x%x\t(0x%lx = 0x%lx + 0x%x)",
					      (uint8_t)c, ip + c, ip, c);
				else
					DEBUG("jmprb\t0x%x\t(0x%lx = 0x%lx - 0x%x)",
					      (uint8_t)c, ip + c, ip, -c);
				ip += c;
			}
#else
			ip += (int8_t)mem[ip];
#endif
			break;
		case VASM_OP_JZB:
			REG1;
			if (!REGI) {
				ip += (signed char)mem[ip];
				DEBUG("jzb\t0x%lx,r%d\t(%lu, true)",
				      ip, regi, REGI);
			} else {
				DEBUG("jzb\t0x%lx,r%d\t(%lu, false)",
				      ip, regi, REGI);
				ip += 1;
			}
			break;
		case VASM_OP_JNZB:
			REG1;
			if (REGI) {
				ip += (signed char)mem[ip];
				DEBUG("jnzb\t0x%lx,r%d\t(%lu, true)",
				      ip, regi, REGI);
			} else {
				DEBUG("jnzb\t0x%lx,r%d\t(%lu, false)",
				      ip, regi, REGI);
				ip += 1;
			}
			break;
		case VASM_OP_JPB:
			REG1;
			if ((ssize_t)REGI > 0) {
				ip += (signed char)mem[ip];
				DEBUG("jpb\t0x%lx,r%d\t(%lu, true)",
				      ip, regi, REGI);
			} else {
				DEBUG("jpb\t0x%lx,r%d\t(%lu, false)",
				      ip, regi, REGI);
				ip += 1;
			}
			break;
		case VASM_OP_JPZB:
			REG1;
			if ((ssize_t)REGI >= 0) {
				ip += (signed char)mem[ip];
				DEBUG("jpzb\t0x%lx,r%d\t(%lu, true)",
				      ip, regi, REGI);
			} else {
				DEBUG("jpzb\t0x%lx,r%d\t(%lu, false)",
				      ip, regi, REGI);
				ip += 1;
			}
			break;
		case VASM_OP_LOADL:
			REG2;
			REGI = *(uint64_t *)(mem + REGJ);
			REGI = be64toh(REGI);
			DEBUG("loadl\tr%d,r%d\t(%lu <-- 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case VASM_OP_LOADLAT:
			REG3;
			REGI = *(uint64_t *)(mem + REGJ + REGK);
			REGI = be64toh(REGI);
			DEBUG("loadlat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)", regi, regj, regk, REGI, REGJ, REGK);
			break;
		case VASM_OP_LOADIAT:
			REG3;
			REGI = *(uint32_t *)(mem + REGJ + REGK);
			REGI = be32toh(REGI);
			DEBUG("loadiat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)", regi, regj, regk, REGI, REGJ, REGK);
			break;
		case VASM_OP_LOADSAT:
			REG3;
			REGI = *(uint16_t *)(mem + REGJ + REGK);
			REGI = be16toh(REGI);
			DEBUG("loadsat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)", regi, regj, regk, REGI, REGJ, REGK);
			break;
		case VASM_OP_LOADBAT:
			REG3;
			REGI = *(uint8_t *)(mem + REGJ + REGK);
			DEBUG("loadbat\tr%d,r%d,r%d\t(%lu <-- 0x%lx + 0x%ld)", regi, regj, regk, REGI, REGJ, REGK);
			break;
		case VASM_OP_STOREL:
			REG2;
			*(uint64_t *)(mem + REGJ) = htobe64(REGI);
			DEBUG("storel\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case VASM_OP_STOREI:
			REG2;
			*(uint32_t *)(mem + REGJ) = htobe32(REGI);
			DEBUG("storei\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case VASM_OP_STORES:
			REG2;
			*(uint16_t *)(mem + REGJ) = htobe16(REGI);
			DEBUG("stores\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case VASM_OP_STOREB:
			REG2;
			*(uint8_t *)(mem + REGJ) = REGI;
			DEBUG("storeb\tr%d,r%d\t(%lu --> 0x%lx)", regi, regj, REGI, REGJ);
			break;
		case VASM_OP_STORELAT:
			REG3;
			*(uint64_t *)(mem + REGJ + REGK) = htobe64(REGI);
			DEBUG("storelat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)", regi, regj, regk, REGI, REGJ, REGK);
			break;
		case VASM_OP_STOREIAT:
			REG3;
			*(uint32_t *)(mem + REGJ + REGK) = htobe32(REGI);
			DEBUG("storeiat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)", regi, regj, regk, REGI, REGJ, REGK);
			break;
		case VASM_OP_STORESAT:
			REG3;
			*(uint16_t *)(mem + REGJ + REGK) = htobe16(REGI);
			DEBUG("storesat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)", regi, regj, regk, REGI, REGJ, REGK);
			break;
		case VASM_OP_STOREBAT:
			REG3;
			*(uint8_t *)(mem + REGJ + REGK) = REGI;
			DEBUG("storebat\tr%d,r%d,r%d\t(%lu --> 0x%lx + 0x%ld)", regi, regj, regk, REGI, REGJ, REGK);
			break;
		case VASM_OP_PUSH:
			REG1;
			*(size_t *)(mem + sp) = REGI;
			DEBUG("push\tr%d\t(%lu)", regi, *(size_t *)(mem + sp));
			sp += sizeof regs[regi];
			break;
		case VASM_OP_POP:
			REG1;
			sp -= sizeof REGI;
			DEBUG("pop\tr%d\t(%ld)", regi, REGI);
			REGI = *(size_t *)(mem + sp);
			break;
		case VASM_OP_MOV:
			REG2;
			REGI = REGJ;
			DEBUG("mov\tr%d,r%d\t(%lu)", regi, regj, REGI);
			break;
		case VASM_OP_SETL:
			REG1;
			val = *(uint64_t *)(mem + ip);
			val = be64toh(val);
			ip += 8;
			REGI = val;
			DEBUG("setl\tr%d,%lu\t(%lu)", regi, val, REGI);
			break;
		case VASM_OP_SETI:
			REG1;
			val = *(uint32_t *)(mem + ip);
			val = be32toh(val);
			ip += 4;
			REGI = val;
			DEBUG("seti\tr%d,%lu\t(%lu)", regi, val, REGI);
			break;
		case VASM_OP_SETS:
			REG1;
			val = *(uint16_t *)(mem + ip);
			val = be16toh(val);
			ip += 2;
			REGI = val;
			DEBUG("sets\tr%d,%lu\t(%lu)", regi, val, REGI);
			break;
		case VASM_OP_SETB:
			REG1;
			val = *(uint8_t *)(mem + ip);
			ip += 1;
			REGI = val;
			DEBUG("setb\tr%d,%lu\t(%lu)", regi, val, REGI);
			break;
		case VASM_OP_ADD:
			REG3OP("add", +);
			break;
		case VASM_OP_SUB:
			REG3OP("sub", -);
			break;
		case VASM_OP_MUL:
			REG3OP("mul", *);
			break;
		case VASM_OP_DIV:
			REG3OP("div", /);
			break;
		case VASM_OP_MOD:
			REG3OPSTR("mod", %, "%%");
			break;
		case VASM_OP_REM:
			REG3OPSTR("rem", %, "%%");
			break;
		case VASM_OP_LSHIFT:
			REG3OP("lshift", <<);
			break;
		case VASM_OP_RSHIFT:
			REG3OP("rshift", >>);
			break;
		case VASM_OP_XOR:
			REG3OP("xor", ^);
			break;
		case VASM_OP_NOT:
			REG2;
			REGI = ~REGJ;
			DEBUG("not\tr%d,r%d\t(%lu)", regi, regj, REGI);
			break;
		case VASM_OP_INV:
			REG2;
			REGI = !REGJ;
			DEBUG("inv\tr%d,r%d\t(%lu)", regi, regj, REGI);
			break;
		case VASM_OP_SYSCALL:
			DEBUG("syscall");
			vasm_syscall();
			break;
		default:
			// Did you know that this print instruction can make all other
			// instructions 22% slower?
			// I wish I were kidding.
#ifndef NDEBUG
			fprintf(stderr, "ERROR: invalid opcode (%u)\n", op);
#endif
#ifdef UNSAFE
			assert(0);
#else
			abort();
#endif
			break;
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

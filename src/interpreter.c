/**
 */

#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <x86intrin.h>
#include "vasm.h"


static char   mem[0x100000];
static long   regs[32];
static size_t ip;


#define REG3OP(op)        \
	regi = mem[ip++]; \
	regj = mem[ip++]; \
	regk = mem[ip++]; \
	regs[regi] = regs[regj] op regs[regk]

#define REG32STR(m,op) DEBUG(m "\tr%d,r%d,r%d\t(%lu = %lu " op " %lu)", \
                             regi, regj, regk, regs[regi], regs[regj], regs[regk])

#ifndef NDEBUG
# define DEBUG(x, ...) fprintf(stderr, x "\n", ##__VA_ARGS__)
#else
# define DEBUG(x, ...) do {} while (0)
#endif

#define sp regs[31]


#ifndef NOPROF
__attribute__((always_inline))
static inline unsigned long rdtsc()
{
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((unsigned long)hi << 32) | lo;
}
static size_t icounter;
static size_t rstart;
#endif



static void vasm_syscall() {
	switch (regs[0]) {
	case 0:
#ifndef NOPROF
		; size_t r = rdtsc() - rstart;
		printf("\n");
		printf("Instructions executed: %lu\n", icounter);
		printf("Host CPU cycles: %lu\n", r);
		printf("Average host CPU cycles per instruction: %lf\n", (double)r / icounter);
#endif
		exit(regs[1]);
		break;
	case 1:
		regs[0] = write(regs[1],
		                (char *)(mem + regs[2]),
		                regs[3]);
		DEBUG("write(%lu, 0x%lx, %lu) = %ld",
		        regs[1], regs[2], regs[3], regs[0]);
		break;
	default:
		regs[0] = -1;
		break;
	}
}


static void run() {

	void *code  = mem;
	sp = 0x10000;

#ifndef NOPROF
	rstart = rdtsc();
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
			regi = mem[ip];
			ip++;
			if (!regs[regi]) {
				ip = *(size_t *)(mem + ip);
				ip = be64toh(ip);
				DEBUG("jz\t0x%lx,r%d\t(%lu, true)",
				      ip, regi, regs[regi]);
			} else {
#ifndef NDEBUG
				size_t v = *(size_t *)(mem + ip);
				v = be64toh(ip);
#endif
				DEBUG("jz\t0x%lx,r%d\t(%lu, false)",
				      ip, regi, regs[regi]);
				ip += sizeof ip;
			}
			break;
		case VASM_OP_JNZ:
			regi = mem[ip];
			ip++;
			if (regs[regi]) {
				ip = *(size_t *)(mem + ip);
				ip = be64toh(ip);
				DEBUG("jnz\t0x%lx,r%d\t(%lu, true)",
				      ip, regi, regs[regi]);
			} else {
#ifndef NDEBUG
				size_t v = *(size_t *)(mem + ip);
				v = be64toh(ip);
#endif
				DEBUG("jnz\t0x%lx,r%d\t(%lu, false)",
				      ip, regi, regs[regi]);
				ip += sizeof ip;
			}
			break;
		case VASM_OP_LOAD:
			regi = mem[ip];
			ip++;
			regj = mem[ip];
			ip++;
			regs[regi] = *(size_t *)(mem + regs[regj]);
			regs[regi] = be64toh(regs[regi]);
			DEBUG("load\tr%d,r%d\t(%lu)", regi, regj, regs[regi]);
			break;
		case VASM_OP_STORE:
			regi = mem[ip];
			ip++;
			regj = mem[ip];
			ip++;
			*(size_t *)(mem + regs[regj]) = htobe64(regs[regi]);
			DEBUG("store\tr%d,r%d\t(%lu)", regi, regj, be64toh(*(size_t *)(mem + regs[regj])));
			break;
		case VASM_OP_PUSH:
			regi = mem[ip];
			ip++;
			*(size_t *)(mem + sp) = regs[regi];
			DEBUG("push\tr%d\t(%lu)", regi, mem[sp]);
			sp += sizeof regs[regi];
			break;
		case VASM_OP_POP:
			regi = mem[ip];
			ip++;
			sp -= sizeof regs[regi];
			DEBUG("pop\tr%d", regi);
			regs[regi] = *(size_t *)(mem + sp);
			break;
		case VASM_OP_MOV:
			regi = mem[ip];
			ip++;
			regj = mem[ip];
			ip++;
			regs[regi] = regs[regj];
			DEBUG("mov\tr%d,r%d\t(%lu)", regi, regj, regs[regi]);
			break;
		case VASM_OP_SET:
			regi = mem[ip];
			ip++;
			val = *(size_t *)(mem + ip);
			val = be64toh(val);
			ip += sizeof val;
			regs[regi] = val;
			DEBUG("set\tr%d,%lu\t(%lu)", regi, val, regs[regi]);
			break;
		case VASM_OP_ADD:
			REG3OP(+);
			REG32STR("add", "+");
			break;
		case VASM_OP_SUB:
			REG3OP(-);
			REG32STR("sub", "-");
			break;
		case VASM_OP_MUL:
			REG3OP(*);
			REG32STR("mul", "*");
			break;
		case VASM_OP_DIV:
			REG3OP(/);
			REG32STR("div", "/");
			break;
		case VASM_OP_MOD:
			REG3OP(%);
			REG32STR("mod", "%%");
			break;
		case VASM_OP_REM:
			REG3OP(%);
			REG32STR("rem", "%%");
			break;
		case VASM_OP_LSHIFT:
			REG3OP(<<);
			REG32STR("lshift", "<<");
			break;
		case VASM_OP_RSHIFT:
			REG3OP(>>);
			REG32STR("rshift", ">>");
			break;
		case VASM_OP_XOR:
			REG3OP(^);
			REG32STR("xor", "^");
			break;
		case VASM_OP_NOT:
			regi = mem[ip];
			ip++;
			regj = mem[ip];
			ip++;
			regs[regi] = ~regs[regj];
			DEBUG("not\tr%d,r%d\t(%lu)", regi, regj, regs[regi]);
			break;
		case VASM_OP_INV:
			regi = mem[ip];
			ip++;
			regj = mem[ip];
			ip++;
			regs[regi] = !regs[regj];
			DEBUG("inv\tr%d,r%d\t(%lu)", regi, regj, regs[regi]);
			break;
		case VASM_OP_SYSCALL:
			DEBUG("syscall");
			vasm_syscall();
			break;
		default:
			DEBUG("ERROR: invalid opcode (%lu)", op);
			abort();
		}
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

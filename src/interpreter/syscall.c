#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "interpreter/syscall.h"
#include "util.h"

void vasm_syscall(int64_t regs[32], uint8_t *mem) {
	switch (regs[0]) {
	case 0: // exit(code)
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
		regs[0] = -1;
		break;
	case 9: // signal
		switch (regs[1]) {
		case 9:
			abort();
			break;
		default:
			regs[0] = -1;
			return;
		}
	default:
		regs[0] = -1;
		break;
	}
}




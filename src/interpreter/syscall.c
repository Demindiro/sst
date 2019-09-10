#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "interpreter/syscall.h"
#include "util.h"

#ifndef NDEBUG
# ifdef DEBUG
#  undef DEBUG
# endif
# define DEBUG(m, ...) fprintf(stderr, m "\n", ##__VA_ARGS__)
#endif


void vasm_syscall(int64_t regs[32], uint8_t *mem) {
	int fd;
	struct sockaddr_in6 addr;
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
		goto _default;
	case 4: // listen(ip6, port)
		fd = socket(AF_INET6, SOCK_STREAM, 0);
		if (fd < 0)
			goto _default;
		addr.sin6_family = AF_INET6;
		addr.sin6_port   = htons(regs[2]);
		memcpy(&addr.sin6_addr, (void *)(mem + regs[3]), 16);
#ifndef NDEBUG
		char buf[64];
		inet_ntop(AF_INET6, &addr.sin6_addr, buf, sizeof buf);
#endif
		if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
			close(fd);
			DEBUG("listen(\"%s\", %lu) = -1", buf, regs[2]);
			goto _default;
		}
		if (listen(fd, 5) < 0) {
			close(fd);
			DEBUG("listen(\"%s\", %lu) = -1", buf, regs[2]);
			goto _default;
		}
		DEBUG("listen(\"%s\", %lu) = %d", buf, regs[2], fd);
		regs[0] = fd;
		break;
	case 5: // accept(fd)
#ifndef NDEBUG
		fprintf(stderr, "accept(%ld) = ...", regs[1]);
#endif
		regs[0] = accept(regs[1], NULL, NULL);
		DEBUG("\33[2K\raccept(%ld) = %ld", regs[1], regs[0]);
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
	_default:
		regs[0] = -1;
		break;
	}
}




/**
 * Dump the contents of an object or executable
 */


#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "vasm.h"




int main(int argc, char **argv)
{
	if (argc < 0) {
		fprintf(stderr, "Usage: %s <file>\n", argc > 0 ? argv[0] : "dump");
		return 2;
	}

	int fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("Failed to open file");
		return 1;
	}

#define ERROR_EOF(l) do {				\
	if (n < (l)) {					\
		fprintf(stderr, "Unexpected EOF");	\
		return 1;				\
	}						\
} while (0)

	uint32_t magic;
	size_t n = read(fd, &magic, sizeof magic);
	ERROR_EOF(sizeof magic);
	magic = be32toh(magic);

	struct lblpos lbl2pos[4096];
	size_t lbl2poscount = 0;
	struct lblpos pos2lbl[4096];
	size_t pos2lblcount = 0;

	if (magic == 0x55002019) {
		// yey
	} else if (magic == 0x55102019) {
		uint32_t l; 
		// Pos to lbl
		n = read(fd, &l, sizeof l);
		ERROR_EOF(sizeof l);
		l = be32toh(l);
		for (size_t i = 0; i < l; i++) {
			uint8_t strl;
			n = read(fd, &strl, sizeof strl);
			ERROR_EOF(sizeof strl);
			char b[256];
			n = read(fd, &b, strl);
			ERROR_EOF(strl);
			b[n] = 0;
			uint64_t p;
			n = read(fd, &p, sizeof p);
			ERROR_EOF(sizeof p);
			p = be64toh(p);
			lbl2pos[lbl2poscount].pos = p;
			lbl2pos[lbl2poscount].lbl = strclone(b);
			lbl2poscount++;
		}
		// Lbl to pos
		n = read(fd, &l, sizeof l);
		ERROR_EOF(sizeof l);
		l = be32toh(l);
		for (size_t i = 0; i < l; i++) {
			uint8_t strl;
			n = read(fd, &strl, sizeof strl);
			ERROR_EOF(sizeof strl);
			char b[256];
			n = read(fd, &b, strl);
			ERROR_EOF(strl);
			b[n] = 0;
			uint64_t p;
			n = read(fd, &p, sizeof p);
			ERROR_EOF(sizeof p);
			p = be64toh(p);
			pos2lbl[pos2lblcount].pos = p;
			pos2lbl[pos2lblcount].lbl = strclone(b);
			pos2lblcount++;
		}
	} else {
		fprintf(stderr, "Invalid magic number: 0x%08x\n", magic);
		return 1;
	}

	size_t i = 0, k = 0;
	while (1) {
		unsigned char buf[0x1000];
		char b[0x100], c[0x100];
		n = read(fd, buf + i, sizeof buf - i);
		i = 0;

		if (n == 0)
			break;
		if (n == -1) {
			perror("Failed to read file");
			return 1;
		}

#define CHECK(_) do {		\
	l = _;			\
	if (l > n - i) {	\
		i = n - i;	\
		goto nextbatch;	\
	}			\
	i++;			\
} while (0);

		while (1) {

			for (size_t j = 0; j < lbl2poscount; j++) {
				if (lbl2pos[j].pos == k)
					printf("%s:\n", lbl2pos[j].lbl);
			}

			unsigned char op = buf[i];
			union vasm_all a = { .op = op };

			size_t l;
			uint64_t u64;
			uint32_t u32;
			uint16_t u16;
			uint8_t  u8;
			switch (get_vasm_args_type(op)) {
			case ARGS_TYPE_NONE:
				CHECK(1);
				break;
			case ARGS_TYPE_REG1:
				CHECK(2);
				a.r.r = buf[i++];
				break;
			case ARGS_TYPE_REG2:
				CHECK(3);
				a.r2.r0 = buf[i++];
				a.r2.r1 = buf[i++];
				break;
			case ARGS_TYPE_REG3:
				CHECK(4);
				a.r3.r0 = buf[i++];
				a.r3.r1 = buf[i++];
				a.r3.r2 = buf[i++];
				break;
			case ARGS_TYPE_BYTE:
				CHECK(2);
				u8 = buf[i++];
				snprintf(b, sizeof b, "0x%x  (%d, %u)", u8, (int8_t)u8, u8);
				a.s.s = b;
				break;
			case ARGS_TYPE_SHORT:
				CHECK(3);
				u16 = *(uint16_t *)(buf + i);
				u16 = be16toh(u16);
				i += 2;
				snprintf(b, sizeof b, "0x%x  (%d, %u)", u16, (int16_t)u16, u16);
				a.s.s = b;
				break;
			case ARGS_TYPE_INT:
				CHECK(5);
				u32 = *(uint32_t *)(buf + i);
				u32 = be32toh(u32);
				i += 4;
				snprintf(b, sizeof b, "0x%x  (%d, %u)", u32, (int32_t)u32, u32);
				a.s.s = b;
				break;
			case ARGS_TYPE_LONG:
				CHECK(9);
				u64 = *(uint64_t *)(buf + i);
				u64 = be64toh(u64);
				i += 8;
				snprintf(b, sizeof b, "0x%lx  (%ld, %lu)", u64, (int64_t)u64, u64);
				a.s.s = b;
				break;
			case ARGS_TYPE_REGBYTE:
				CHECK(3);
				a.rs.r = buf[i++];
				u8 = buf[i++];
				snprintf(b, sizeof b, "0x%x  (%d, %u)", u8, (int8_t)u8, u8);
				a.rs.s = b;
				break;
			case ARGS_TYPE_REGSHORT:
				CHECK(4);
				a.rs.r = buf[i++];
				u16 = *(uint16_t *)(buf + i);
				u16 = be16toh(u16);
				i += 2;
				snprintf(b, sizeof b, "0x%x  (%d, %u)", u16, (int16_t)u16, u16);
				a.rs.s = b;
				break;
			case ARGS_TYPE_REGINT:
				CHECK(6);
				a.rs.r = buf[i++];
				u32 = *(uint32_t *)(buf + i);
				u32 = be32toh(u32);
				i += 4;
				snprintf(b, sizeof b, "0x%x  (%d, %u)", u32, (int32_t)u32, u32);
				a.rs.s = b;
				break;
			case ARGS_TYPE_REGLONG:
				CHECK(10);
				a.rs.r = buf[i++];
				u64 = *(uint64_t *)(buf + i);
				u64 = be64toh(u64);
				i += 8;
				snprintf(b, sizeof b, "0x%lx  (%ld, %lu)", u64, (int64_t)u64, u64);
				a.rs.s = b;
				break;
			case -1:
			default:
				CHECK(1);
				snprintf(c, sizeof c, "??? (%u, 0x%x)", op, op);
				goto undefop;
			}

			vasm2str(a, c, sizeof c);
		undefop:
			printf("%6lx   ", k);
			for (size_t k = 0; k < l; k++)
				printf("%02x ", buf[i - l + k]);
			for (size_t k = l; k < 10; k++)
				printf("   ");
			printf(" \t%s\n", c);

			k += l;
		}
		nextbatch:;
	}
}

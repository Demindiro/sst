#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <stdint.h>
#include "hashtbl.h"


int main(int argc, char **argv) {
	
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <input ...> output\n", argv[0]);
	}

	char buf[0x10000];
	struct hashtbl lbl2pos;
	struct lblpos  pos2lbl[0x10000];
	char vbin[0x10000];
	vbin[0] = VASM_OP_JMP;
	pos2lbl[0].lbl = "_start";
	pos2lbl[0].pos = 1;
	size_t vbinlen = 9;
	size_t pos2lblcount = 1;

	h_create(&lbl2pos, 32);

	for (size_t i = 1; i < argc - 1; i++) {
		printf("%s:\n", argv[i]);
		int fd = open(argv[i], O_RDONLY);
		size_t len = read(fd, buf, sizeof buf);
		close(fd);

		char *ptr = buf + 4;

		printf("  lbl2pos:\n");
		uint32_t l = be32toh(*(uint32_t *)ptr);
		ptr += sizeof l;
		for (size_t i = 0; i < l; i++) {
			uint8_t strl = *ptr;
			ptr += sizeof strl;
			char *s = malloc(strl + 1);
			if (s == NULL) {
				perror("malloc");
				return 1;
			}
			memcpy(s, ptr, strl);
			s[strl] = 0;
			ptr += strl;
			uint64_t pos = *(uint64_t *)ptr;
			pos = be64toh(pos);
			ptr += sizeof pos;
			printf("    %s = %lu (%lu + %lu)\n", s, pos + vbinlen,
			       vbinlen, pos);
			if (h_add(&lbl2pos, s, pos + vbinlen)) {
				perror("h_add");
				return 1;
			}
		}

		printf("  pos2lbl:\n");
		l = be32toh(*(uint32_t *)ptr);
		ptr += sizeof l;
		for (size_t i = 0; i < l; i++) {
			uint8_t strl = *ptr;
			ptr += sizeof strl;
			char *s = malloc(strl + 1);
			if (s == NULL) {
				perror("malloc");
				return 1;
			}
			memcpy(s, ptr, strl);
			s[strl] = 0;
			ptr += strl;
			uint64_t pos = *(uint64_t *)ptr;
			pos = be64toh(pos);
			ptr += sizeof pos;
			printf("    %lu (%lu + %lu) = %s\n", pos + vbinlen, vbinlen, pos, s);
			pos2lbl[pos2lblcount].lbl = s;
			pos2lbl[pos2lblcount].pos = pos + vbinlen;
			pos2lblcount++;
		}

		len -= ptr - buf;
		memcpy(vbin + vbinlen, ptr, len);
		vbinlen += len;
	}

	printf("%s\n", argv[argc - 1]);
	for (size_t i = 0; i < pos2lblcount; i++) {
		size_t pos = h_get(&lbl2pos, pos2lbl[i].lbl);
		*(size_t *)(vbin + pos2lbl[i].pos) = htobe64(pos);
		printf("  %s @ %lu (0x%lx)--> %lu (0x%lx)\n", pos2lbl[i].lbl,
		       pos2lbl[i].pos, pos2lbl[i].pos, pos, pos);
	}

	// Write binary shit
	int fd = open(argv[argc - 1], O_WRONLY | O_CREAT | O_TRUNC, 0755);
	write(fd, "\x55\x00\x20\x19", 4); // Magic number
	write(fd, vbin, vbinlen);
	close(fd);

	// Yay
	return 0;
}

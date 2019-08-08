#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include "vasm.h"
#include "text2vasm.h"
#include "vasm2vbin.h"


#define FUNC_LINE_NONE   0
#define FUNC_LINE_FUNC   1


union vasm_all vasms[4096];
size_t vasmcount;

char vbin[0x10000];
size_t vbinlen;

struct lblpos lbl2pos[4096];
size_t lbl2poscount;
struct lblpos pos2lbl[4096];
size_t pos2lblcount;


int main(int argc, char **argv) {
	
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <input> <output>", argv[0]);
		return 1;
	}

	// Read source
	char buf[0x10000];
	int fd = open(argv[1], O_RDONLY);
	size_t len = read(fd, buf, sizeof buf);
	close(fd);

	printf("=== text2vasm ===\n");
	if (text2vasm(buf, len) < 0)
		return 1;
	printf("\n");

	// x. vasm2vbin
	printf("=== vasm2vbin ===\n");
	vasm2vbin();
	printf("\n");
	for (size_t i = 0; i < lbl2poscount; i++)
		printf("%s --> %lu\n", lbl2pos[i].lbl, lbl2pos[i].pos);
	printf("\n");
	for (size_t i = 0; i < pos2lblcount; i++)
		printf("%lu --> %s\n", pos2lbl[i].pos, pos2lbl[i].lbl);
	printf("\n");
	size_t i = 0, j = 0;
	while (i < vbinlen) {
		for (size_t k = 0; k < 4 && i < vbinlen; k++) {
			for (size_t l = 0; l < 2 && i < vbinlen; l++) {
				printf("%02x", (unsigned char)vbin[i]);
				i++;
			}
			printf(" ");
		}
		for ( ; i % 8 != 0; i++) {
			if (i % 2 == 0)
				printf(" ");
			printf("  ");
		}
		printf("| ");
		for (size_t k = 0; k < 8 && j < vbinlen; k++) {
			char c = vbin[j];
			printf("%c", (' ' <= c && c <= '~') ? c : '.');
			j++;
		}
		printf("\n");
	}

	// Write binary shit
	fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0755);
	write(fd, "\x55\x10\x20\x19", 4); // Magic number
	dumplbl(fd);
	write(fd, vbin, vbinlen);
	close(fd);

	// Yay
	return 0;
}

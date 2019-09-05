#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include "util.h"
#include "vasm.h"
#include "text2vasm.h"
#include "vasm2vbin.h"


union vasm_all vasms[4096];
size_t vasmcount;

char vbin[0x10000];
size_t vbinlen;

struct lblmap map;


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

	if (text2vasm(buf, len, vasms, &vasmcount) < 0)
		return 1;


	vasm2vbin(vasms, vasmcount, vbin, &vbinlen, &map);
	DEBUG("Label to position mapping");
	for (size_t i = 0; i < map.lbl2poscount; i++)
		DEBUG("%-16s @ 0x%lx", map.lbl2pos[i].lbl, map.lbl2pos[i].pos);
	DEBUG("");
	DEBUG("Positions to be filled in");
	for (size_t i = 0; i < map.pos2lblcount; i++)
		DEBUG("0x%lx @ %s", map.pos2lbl[i].pos, map.pos2lbl[i].lbl);
	DEBUG("");
	size_t i = 0, j = 0;
	DEBUG("Binary length: %lu", vbinlen);
	DEBUG("");
#ifndef NDEBUG
	while (i < vbinlen) {
		fprintf(stderr, "DEBUG: ");
		fprintf(stderr, "0x%06lx  ", i);
		for (size_t k = 0; k < 8 && i < vbinlen; k++) {
			for (size_t l = 0; l < 2 && i < vbinlen; l++) {
				fprintf(stderr, "%02x", (unsigned char)vbin[i]);
				i++;
			}
			fprintf(stderr, " ");
		}
		for ( ; i % 16 != 0; i++) {
			if (i % 2 == 0)
				fprintf(stderr, " ");
			fprintf(stderr, "  ");
		}
		fprintf(stderr, "| ");
		for (size_t k = 0; k < 16 && j < vbinlen; k++) {
			char c = vbin[j];
			fprintf(stderr, "%c", (' ' <= c && c <= '~') ? c : '.');
			j++;
		}
		fprintf(stderr, "\n");
	}
#endif

	fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0755);
	write(fd, "\x55\x10\x20\x19", 4); // Magic number
	dumplbl(fd, &map);
	write(fd, vbin, vbinlen);
	close(fd);

	return 0;
}

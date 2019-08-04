#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <stdint.h>
#include "vasm.h"


#define streq(a,b) (strcmp(a,b) == 0)



struct hashtbl {
	size_t  len;
	size_t  count;
	void ***arrays;
};


size_t h_hash_str(char *str)
{
	size_t h = 0;
	for (char *c = str; *c != 0; c++) {
		h += *c;
	}
	return h;
}


int h_create(struct hashtbl *tbl, size_t size)
{
	tbl->len    = size;
	tbl->count  = 0;
	tbl->arrays = calloc(size, sizeof *tbl->arrays);
	return tbl->arrays == NULL ? -1 : 0;
}


void h_destroy(struct hashtbl *tbl)
{
	for (size_t i = 0; i < tbl->len; i++)
		free(tbl->arrays[i]);
	free(tbl->arrays);
}


int h_add(struct hashtbl *tbl, char *str, size_t val);


int h_resize(struct hashtbl *tbl, size_t newlen)
{
	struct hashtbl ntbl;
	if (h_create(&ntbl, newlen) < 0)
		return -1;
	for (size_t i = 0; i < tbl->len; i++) {
		void **a = tbl->arrays[i];
		if (a == NULL)
			continue;
		size_t l = *(size_t *)a;
		size_t lcap = l & 0xffffffff, lcount = l >> 32L;
		for (size_t j = 0; j < lcount; j++) {
			if (h_add(&ntbl, (char *)a[1 + l * 2], (size_t)a[1 + l * 2 + 1]) < 0) {
				h_destroy(&ntbl);
				// TODO restore old table
				return -1;
			}
		}
		free(a);
		tbl->arrays[i] = NULL;
	}
	h_destroy(tbl);
	*tbl = ntbl;
	return 0;
}


int h_add(struct hashtbl *tbl, char *str, size_t val)
{
	size_t k = h_hash_str(str) % tbl->len;
	void **a = tbl->arrays[k];

	if (a == NULL) {
		a = calloc(sizeof(size_t) + 2 * sizeof(void *), 1);
		if (a == NULL)
			return -1;
		*(size_t *)a = 1L;
		tbl->arrays[k] = a;
	}

	size_t l = *(size_t *)a;
	size_t lcap = l & 0xffffffff, lcount = l >> 32L;

	if (l >= 4 && tbl->len < tbl->count * 2) {
		if (h_resize(tbl, tbl->count * 2) < 0)
			return -1;
		k = h_hash_str((char *)a[l]) % tbl->len;
		a = tbl->arrays[k];
		l = *(size_t *)a;
		lcap = l & 0xffffffff, lcount = l >> 32L;
	}

	if (lcount >= lcap) {
		size_t nlcap = lcap * 2;
		void **b = realloc(a, sizeof(size_t) + 2 * nlcap * sizeof(void *));
		if (b == NULL)
			return -1;
		tbl->arrays[k] = a = b;
		lcap = nlcap;
	}

	((char  **)a)[1 + lcount * 2    ] = str;
	((size_t *)a)[1 + lcount * 2 + 1] = val;
	lcount++;
	((size_t *)a)[0] = lcap | (lcount << 32L);

	return 0;
}


size_t h_get(struct hashtbl *tbl, char *str)
{
	size_t k = h_hash_str(str) % tbl->len;
	void **a = tbl->arrays[k];
	if (a == NULL)
		return -1;

	size_t l = *(size_t *)a;
	size_t lcount = l >> 32L;

	for (size_t i = 0; i < lcount; i++) {
		if (strcmp(a[1 + i * 2], str) == 0)
			return (size_t)a[1 + i * 2 + 1];
	}
	
	return -1;
}






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

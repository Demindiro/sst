#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <stdint.h>
#include "hashtbl.h"


static size_t h_hash_str(const char *str)
{
	size_t h = 0;
	for (const char *c = str; *c != 0; c++) {
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


int h_get2(struct hashtbl *tbl, const char *str, size_t *val)
{
	size_t k = h_hash_str(str) % tbl->len;
	void **a = tbl->arrays[k];
	if (a == NULL)
		return -1;

	size_t l = *(size_t *)a;
	size_t lcount = l >> 32L;

	for (size_t i = 0; i < lcount; i++) {
		if (strcmp(a[1 + i * 2], str) == 0) {
			*val = (size_t)a[1 + i * 2 + 1];
			return 0;
		}
	}
	
	return -1;
}


void h_rem(struct hashtbl *tbl, const char *str)
{
	size_t k = h_hash_str(str) % tbl->len;
	void **a = tbl->arrays[k];
	if (a == NULL)
		return;

	size_t l = *(size_t *)a;
	size_t lcount = l >> 32L;

	for (size_t i = 0; i < lcount; i++) {
		if (strcmp(a[1 + i * 2], str) == 0) {
			lcount--;
			memmove(a + 1 + i * 2, a + 1 + (i + 1) * 2, (lcount - i) * 2 * sizeof *a);
			l = (lcount << 32L) | (l & 0xFFFFFFFF);
			*(size_t *)a = l;
		}
	}
}

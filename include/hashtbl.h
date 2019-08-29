#ifndef HASHTABLE_H
#define HASHTABLE_H


#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <stdint.h>
#include "vasm.h"



typedef struct hashtbl {
	size_t  len;
	size_t  count;
	void ***arrays;
} hashtbl_t, *hashtbl;



int h_create(struct hashtbl *tbl, size_t size);


void h_destroy(struct hashtbl *tbl);


int h_resize(struct hashtbl *tbl, size_t newlen);


int h_add(struct hashtbl *tbl, const char *str, size_t val);


size_t h_get(struct hashtbl *tbl, const char *str);


int h_get2(struct hashtbl *tbl, const char *str, size_t *val);


void h_rem(struct hashtbl *tbl, const char *str);


#endif

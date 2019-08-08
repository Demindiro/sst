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


#define streq(a,b) (strcmp(a,b) == 0)



struct hashtbl {
	size_t  len;
	size_t  count;
	void ***arrays;
};


size_t h_hash_str(char *str);


int h_create(struct hashtbl *tbl, size_t size);


void h_destroy(struct hashtbl *tbl);


int h_resize(struct hashtbl *tbl, size_t newlen);


int h_add(struct hashtbl *tbl, char *str, size_t val);


size_t h_get(struct hashtbl *tbl, char *str);


void h_rem(struct hashtbl *tbl, char *str);


#endif

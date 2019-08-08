#ifndef VASM2VBIN_H
#define VASM2VBIN_H


#include "vasm.h"


int vasm2vbin(const union vasm_all *vasms, size_t vasmcount, char *vbin, size_t *vbinlen, struct lblmap *map);


int dumplbl(int fd, struct lblmap *map);


#endif

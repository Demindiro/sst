#ifndef TEXT2VASM_H
#define TEXT2VASM_H


#include <stdint.h>
#include "vasm.h"


int getop(const char *mnem);

int parse_op_args(union vasm_all *v, const char *args);

int text2vasm(char *buf, size_t len, union vasm_all *vasms, size_t *vasmcount);


#endif

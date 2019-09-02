#ifndef STRUCTS2VASM_H
#define STRUCTS2VASM_H

#include <stddef.h>
#include "func.h"

int func2vasm(union vasm_all **vasms, size_t *vasmcount, struct func *f);

#endif

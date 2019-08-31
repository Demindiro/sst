#ifndef OPTIMIZE_LINES_H
#define OPTIMIZE_LINES_H

#include "func.h"

extern enum optimize_lines_options {
	UNUSED_ASSIGN          = 1L << 0,
	EARLY_DESTROY          = 1L << 1,
	CONSTANT_IF            = 1L << 2,
	FAST_DIV               = 1L << 3,
	NOP_MATH               = 1L << 4,
	SUBSTITUTE_VAR         = 1L << 5,
	INVERSE_MATH_IF        = 1L << 6,
	SUBSTITUTE_TEMP_IF_VAR = 1L << 7,
	FINDCONST              = 1L << 8,
	INVERT_IF              = 1L << 9,
	UNUSED_LABEL           = 1L << 10,
	PRECOMPUTE_MATH        = 1L << 11,
	UNUSED_DECLARE         = 1L << 12,
	IMMEDIATE_GOTO         = 1L << 13,
} optimize_lines_options;

int optimizefunc(struct func *f);

#endif

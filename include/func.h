#ifndef FUNC_H
#define FUNC_H


#define FUNC_LINE_NONE    0
#define FUNC_LINE_ASSIGN  1
#define FUNC_LINE_DECLARE 2
#define FUNC_LINE_DESTROY 3
#define FUNC_LINE_FUNC    4
#define FUNC_LINE_GOTO    5
#define FUNC_LINE_IF      6
#define FUNC_LINE_LABEL   7
#define FUNC_LINE_LOAD    8
#define FUNC_LINE_MATH    9
#define FUNC_LINE_RETURN 10
#define FUNC_LINE_STORE  11

#define MATH_ADD    VASM_OP_ADD
#define MATH_SUB    VASM_OP_SUB
#define MATH_MUL    VASM_OP_MUL
#define MATH_DIV    VASM_OP_DIV
#define MATH_MOD    VASM_OP_MOD
#define MATH_NOT    VASM_OP_NOT
#define MATH_INV    VASM_OP_INV
#define MATH_RSHIFT VASM_OP_RSHIFT
#define MATH_LSHIFT VASM_OP_LSHIFT
#define MATH_AND    VASM_OP_AND
#define MATH_OR     VASM_OP_OR
#define MATH_XOR    VASM_OP_XOR
#define MATH_LOADAT VASM_OP_LOADLAT
#define MATH_LESS   VASM_OP_LESS
#define MATH_LESSE  VASM_OP_LESSE


struct func_line {
	char type;
};

struct func_line_assign {
	struct func_line line;
	const char *var;
	const char *value;
	char cons;
};

struct func_line_declare {
	struct func_line line;
	const char *type;
	const char *var;
};

struct func_line_func {
	struct func_line line;
	unsigned char paramcount;
	const char  *name;
	const char **params;
	const char  *var;
};

struct func_line_goto {
	struct func_line line;
	const char *label;
};

struct func_line_if {
	struct func_line line;
	const char *label;
	const char *var;
	char inv;
};

struct func_line_label {
	struct func_line line;
	const char *label;
};

struct func_line_math {
	struct func_line line;
	char op;
	const char *x, *y, *z;
};

struct func_line_return {
	struct func_line line;
	const char *val;
};

struct func_line_store {
	struct func_line line;
	const char *var;
	const char *val;
	const char *index;
};

union func_line_all_p {
	struct func_line *line;
	struct func_line_assign  *a;
	struct func_line_declare *d;
	struct func_line_func    *f;
	struct func_line_goto    *g;
	struct func_line_if      *i;
	struct func_line_label   *l;
	struct func_line_math    *m;
	struct func_line_return  *r;
	struct func_line_store   *s;
};


struct func_arg {
	const char *type;
	const char *name;
};


struct func {
	const char *type;
	const char *name;
	unsigned char argcount;
	unsigned char linecount;
	struct func_arg   args[16];
	struct func_line *lines[256];
};


#endif

#ifndef FUNC_H
#define FUNC_H

#include "hashtbl.h"
#include "util.h"
#include "vasm.h"


enum func_type {
	NONE,
	ASSIGN,
	ASM,
	DECLARE,
	DESTROY,
	FUNC,
	GOTO,
	IF,
	LABEL,
	LOAD,
	MATH,
	RETURN,
	STORE,
	THROW,
};

#define MATH_ADD    OP_ADD
#define MATH_SUB    OP_SUB
#define MATH_MUL    OP_MUL
#define MATH_DIV    OP_DIV
#define MATH_MOD    OP_MOD
#define MATH_REM    OP_REM
#define MATH_NOT    OP_NOT
#define MATH_INV    OP_INV
#define MATH_RSHIFT OP_RSHIFT
#define MATH_LSHIFT OP_LSHIFT
#define MATH_AND    OP_AND
#define MATH_OR     OP_OR
#define MATH_XOR    OP_XOR
#define MATH_L_AND  OP_AND
#define MATH_L_OR   OP_OR
#define MATH_LOADAT (-43) // IDK man
#define MATH_LESS   OP_LESS
#define MATH_LESSE  OP_LESSE


struct func_line {
	enum func_type type;
};

struct func_line_assign {
	enum func_type type;
	const char *var;
	const char *value;
	char cons;
};

struct func_line_asm {
	enum func_type type;
	const char**vasms;
	size_t      vasmcount;
	const char *invars [32];
	char        inregs [32];
	size_t      incount;
	const char *outvars[32];
	char        outregs[32];
	size_t      outcount;
};

struct func_line_declare {
	enum func_type _type;
	const char *type;
	const char *var;
};

#define func_line_destroy func_line_declare

struct func_line_func {
	enum func_type type;
	unsigned char argcount;
	const char  *name;
	const char **args;
	const char  *var;
};

struct func_line_goto {
	enum func_type type;
	const char *label;
};

struct func_line_if {
	enum func_type type;
	const char *label;
	const char *var;
	char inv;
};

struct func_line_label {
	enum func_type type;
	const char *label;
};

struct func_line_math {
	enum func_type type;
	char op;
	const char *x, *y, *z;
};

struct func_line_rename {
	enum func_type type;
	const char *old, *new;
};

struct func_line_return {
	enum func_type type;
	const char *val;
};

struct func_line_store {
	enum func_type type;
	const char *var;
	const char *val;
	const char *index;
};

union func_line_all_p {
	struct func_line *line;
	struct func_line_assign  *a;
	struct func_line_asm     *as;
	struct func_line_declare *d;
	struct func_line_func    *f;
	struct func_line_goto    *g;
	struct func_line_if      *i;
	struct func_line_label   *l;
	struct func_line_math    *m;
	struct func_line_rename  *rn;
	struct func_line_return  *r;
	struct func_line_store   *s;
};


struct func_arg {
	const char *type;
	const char *name;
};


typedef struct func {
	const char *type;
	const char *name;
	unsigned char argcount;
	size_t linecount, linecap;
	struct func_arg    args[32];
	struct func_line **lines;
} func_t, *func;



// Only used for debugging!
#ifndef NDEBUG
extern thread_local func _current_func;
# define SETCURRENTFUNC(f) _current_func = f
# define CLEARCURRENTFUNC _current_func = NULL
# define FDEBUG(m, ...) DEBUG("[%s] " m, f ? f->name : "", ##__VA_ARGS__)
#else
# define SETCURRENTFUNC(f) NULL
# define CLEARCURRENTFUNC  NULL
# define FDEBUG(m, ...) DEBUG(m, ##__VA_ARGS__)
#endif




void insert_line(func f, struct func_line *l);

void line_assign(func f, const char *var, const char *val);

void line_asm(func f, const char **vasms, size_t vasmcount,
              const char **invars , const char *inregs , size_t incount,
              const char **outvars, const char *outregs, size_t outcount);

void line_declare(func f, const char *name, const char *type);

void line_math(func f, int op, const char *x, const char *y, const char *z);

void line_destroy(func f, const char *var);

void line_function(func f, const char *var, const char *func,
                          size_t argcount, const char **args);

void line_function_parse(func f, const char *var, const char *str,
                         hashtbl functbl, hashtbl vartypes);

void line_goto(func f, const char *label);

void line_if(func f, const char *condition, const char *label, int inv);

void line_label(func f, const char *label);

void line_return(func f, const char *val);

void line_store(func f, const char *arr, const char *index, const char *val);

void line_throw(func f, const char *expr);

const char *new_temp_var(func f, const char *type, const char *name);

struct func_line *copy_line(const struct func_line *l);

void line2str(struct func_line *l, char *buf, size_t bufsize);

#endif

#ifndef VASM_H
#define VASM_H

#include "util.h"


enum vasm_op {
	OP_NOP,

	OP_JMP,
	OP_JZ,
	OP_JNZ,
	OP_JP,
	OP_JPZ,
	OP_CALL,
	OP_RET,
	OP_JMPRB,
	OP_JZB,
	OP_JNZB,
	OP_JPB,
	OP_JPZB,

	OP_LDL,
	OP_LDI,
	OP_LDS,
	OP_LDB,
	OP_STRL,
	OP_STRI,
	OP_STRS,
	OP_STRB,

	OP_LDLAT,
	OP_LDIAT,
	OP_LDSAT,
	OP_LDBAT,
	OP_STRLAT,
	OP_STRIAT,
	OP_STRSAT,
	OP_STRBAT,

	OP_PUSH,
	OP_POP,
	OP_MOV,
	OP_SETL,
	OP_SETI,
	OP_SETS,
	OP_SETB,

	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_REM,
	OP_LSHIFT,
	OP_RSHIFT,
	OP_LROT,
	OP_RROT,
	OP_AND,
	OP_OR,
	OP_XOR,
	OP_NOT,
	OP_INV,
	OP_LESS,
	OP_LESSE,

	OP_SYSCALL,

	OP_OP_LIMIT,

	// Specials
	OP_NONE,
	OP_COMMENT,
	OP_LABEL,
	OP_RAW,
	OP_RAW_LONG,
	OP_RAW_INT,
	OP_RAW_SHORT,
	OP_RAW_BYTE,
	OP_RAW_STR,
	OP_SET,
};


struct vasm {
	short op;
};

struct vasm_str {
	short op;
	const char *s;
};

struct vasm_reg {
	short op;
	char  r;
};

struct vasm_reg2 {
	short op;
	char  r0, r1;
};

struct vasm_reg3 {
	short op;
	char  r0, r1, r2;
};

struct vasm_reg_str {
	short op;
	char  r;
	const char *s;
};

union vasm_all {
	short op;
	struct vasm          a;
	struct vasm_reg      r;
	struct vasm_reg2     r2;
	struct vasm_reg3     r3;
	struct vasm_str      s;
	struct vasm_reg_str  rs;
};


struct lblpos {
	const char *lbl;
	size_t      pos;
};


struct lblmap {
	struct lblpos lbl2pos[4096];
	size_t lbl2poscount;
	struct lblpos pos2lbl[4096];
	size_t pos2lblcount;
};



enum vasm_args {
	ARGS_TYPE_SPECIAL,
	ARGS_TYPE_NONE,
	ARGS_TYPE_REG1,
	ARGS_TYPE_REG2,
	ARGS_TYPE_REG3,
	ARGS_TYPE_BYTE,
	ARGS_TYPE_SHORT,
	ARGS_TYPE_INT,
	ARGS_TYPE_LONG,
	ARGS_TYPE_REGBYTE,
	ARGS_TYPE_REGSHORT,
	ARGS_TYPE_REGINT,
	ARGS_TYPE_REGLONG,
};


int get_vasm_args_type(int op);

int vasm2str(union vasm_all a, char *buf, size_t bufsize);


#endif

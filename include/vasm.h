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
	char _[14];
};

struct vasm_str {
	short op;
	const char *str;
};

struct vasm_reg {
	short op;
	char  r;
};

struct vasm_reg2 {
	short op;
	char  r[2];
};

struct vasm_reg3 {
	short op;
	char  r[3];
};

struct vasm_reg_str {
	short op;
	char  r;
	const char *str;
};

struct vasm_reg2_str {
	short op;
	char  r[2];
	const char *str;
};

union vasm_all {
	short op;
	struct vasm          a;
	struct vasm_reg      r;
	struct vasm_reg2     r2;
	struct vasm_reg3     r3;
	struct vasm_str      s;
	struct vasm_reg_str  rs;
	struct vasm_reg2_str r2s;
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
	ARGS_TYPE_SPECIAL = 0,
	ARGS_TYPE_NONE    = 1,
	ARGS_TYPE_REG1    = 2,
	ARGS_TYPE_REG2    = 3,
	ARGS_TYPE_REG3    = 4,
	ARGS_TYPE_VAL     = 5,
	ARGS_TYPE_REGVAL  = 6,
	ARGS_TYPE_VALREG  = 7,
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

static int get_vasm_args_type(int op)
{
	switch (op) {
	case OP_SYSCALL:
	case OP_RET:
		return ARGS_TYPE_NONE;
	case OP_PUSH:
	case OP_POP:
		return ARGS_TYPE_REG1;
	case OP_STRL:
	case OP_STRI:
	case OP_STRS:
	case OP_STRB:
	case OP_LDL:
	case OP_LDI:
	case OP_LDS:
	case OP_LDB:
	case OP_MOV:
	case OP_NOT:
	case OP_INV:
		return ARGS_TYPE_REG2;
	case OP_ADD:
	case OP_SUB:
	case OP_MUL:
	case OP_DIV:
	case OP_MOD:
	case OP_REM:
	case OP_RSHIFT:
	case OP_LSHIFT:
	case OP_AND:
	case OP_OR:
	case OP_XOR:
	case OP_STRLAT:
	case OP_STRIAT:
	case OP_STRSAT:
	case OP_STRBAT:
	case OP_LDLAT:
	case OP_LDIAT:
	case OP_LDSAT:
	case OP_LDBAT:
	case OP_LESS:
	case OP_LESSE:
		return ARGS_TYPE_REG3;
	case OP_JMP:
	case OP_CALL:
		return ARGS_TYPE_VAL;
	case OP_SET:
	case OP_SETB:
	case OP_SETS:
	case OP_SETI:
	case OP_SETL:
		return ARGS_TYPE_REGVAL;
	case OP_JZ:
	case OP_JNZ:
	case OP_JP:
	case OP_JPZ:
		return ARGS_TYPE_VALREG;
	case OP_NONE:
	case OP_COMMENT:
	case OP_LABEL:
	case OP_RAW_LONG:
	case OP_RAW_INT:
	case OP_RAW_SHORT:
	case OP_RAW_BYTE:
	case OP_RAW_STR:
		return ARGS_TYPE_SPECIAL;
	default:
		ERROR("Undefined OP (%d)", op);
		EXIT(1);
	}
}

#pragma GCC diagnostic pop


void vasm2str(union vasm_all a, char *buf, size_t bufsize);


#endif

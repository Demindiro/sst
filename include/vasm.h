#ifndef VASM_H
#define VASM_H

#include "util.h"


#define VASM_OP_NONE      (-1)
#define VASM_OP_COMMENT   (-2)
#define VASM_OP_LABEL     (-3)
#define VASM_OP_RAW       (-4)
#define VASM_OP_RAW_LONG  (-5)
#define VASM_OP_RAW_INT   (-6)
#define VASM_OP_RAW_SHORT (-7)
#define VASM_OP_RAW_BYTE  (-8)
#define VASM_OP_RAW_STR   (-9)
#define VASM_OP_NOP         0

#define VASM_OP_JMP         1
#define VASM_OP_JZ          2
#define VASM_OP_JNZ         3
#define VASM_OP_JP          4
#define VASM_OP_JPZ         5
#define VASM_OP_CALL        6
#define VASM_OP_RET         7
#define VASM_OP_JMPRB      60
#define VASM_OP_JZB        61
#define VASM_OP_JNZB       62
#define VASM_OP_JPB        63
#define VASM_OP_JPZB       64

#define VASM_OP_LOADL      10
#define VASM_OP_LOADI      11
#define VASM_OP_LOADS      12
#define VASM_OP_LOADB      13
#define VASM_OP_STOREL     14
#define VASM_OP_STOREI     15
#define VASM_OP_STORES     16
#define VASM_OP_STOREB     17

#define VASM_OP_LOADLAT    20
#define VASM_OP_LOADIAT    21
#define VASM_OP_LOADSAT    22
#define VASM_OP_LOADBAT    23
#define VASM_OP_STORELAT   24
#define VASM_OP_STOREIAT   25
#define VASM_OP_STORESAT   26
#define VASM_OP_STOREBAT   27

#define VASM_OP_PUSH       30
#define VASM_OP_POP        31
#define VASM_OP_MOV        32
#define VASM_OP_SETL       33
#define VASM_OP_SETI       34
#define VASM_OP_SETS       35
#define VASM_OP_SETB       36
#define VASM_OP_SET      (-30)

#define VASM_OP_ADD        40
#define VASM_OP_SUB        41
#define VASM_OP_MUL        42
#define VASM_OP_DIV        43
#define VASM_OP_MOD        44
#define VASM_OP_REM        45
#define VASM_OP_LSHIFT     46
#define VASM_OP_RSHIFT     47
#define VASM_OP_LROT       48
#define VASM_OP_RROT       49
#define VASM_OP_AND        50
#define VASM_OP_OR         51
#define VASM_OP_XOR        52
#define VASM_OP_NOT        53
#define VASM_OP_INV        54
#define VASM_OP_LESS       55
#define VASM_OP_LESSE      56

#define VASM_OP_SYSCALL   100


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




#define VASM_ARGS_TYPE_SPECIAL 0
#define VASM_ARGS_TYPE_NONE    1
#define VASM_ARGS_TYPE_REG1    2
#define VASM_ARGS_TYPE_REG2    3
#define VASM_ARGS_TYPE_REG3    4
#define VASM_ARGS_TYPE_VAL     5
#define VASM_ARGS_TYPE_REGVAL  6

static int get_vasm_args_type(int op)
{
	switch (op) {
	case VASM_OP_SYSCALL:
	case VASM_OP_RET:
		return VASM_ARGS_TYPE_NONE;
	case VASM_OP_PUSH:
	case VASM_OP_POP:
		return VASM_ARGS_TYPE_REG1;
	case VASM_OP_STOREL:
	case VASM_OP_STOREI:
	case VASM_OP_STORES:
	case VASM_OP_STOREB:
	case VASM_OP_LOADL:
	case VASM_OP_LOADI:
	case VASM_OP_LOADS:
	case VASM_OP_LOADB:
	case VASM_OP_MOV:
	case VASM_OP_NOT:
	case VASM_OP_INV:
		return VASM_ARGS_TYPE_REG2;
	case VASM_OP_ADD:
	case VASM_OP_SUB:
	case VASM_OP_MUL:
	case VASM_OP_DIV:
	case VASM_OP_MOD:
	case VASM_OP_REM:
	case VASM_OP_RSHIFT:
	case VASM_OP_LSHIFT:
	case VASM_OP_XOR:
	case VASM_OP_STORELAT:
	case VASM_OP_STOREIAT:
	case VASM_OP_STORESAT:
	case VASM_OP_STOREBAT:
	case VASM_OP_LOADLAT:
	case VASM_OP_LOADIAT:
	case VASM_OP_LOADSAT:
	case VASM_OP_LOADBAT:
	case VASM_OP_LESS:
	case VASM_OP_LESSE:
		return VASM_ARGS_TYPE_REG3;
	case VASM_OP_JMP:
	case VASM_OP_CALL:
		return VASM_ARGS_TYPE_VAL;
	case VASM_OP_SET:
	case VASM_OP_SETB:
	case VASM_OP_SETS:
	case VASM_OP_SETI:
	case VASM_OP_SETL:
		return VASM_ARGS_TYPE_REGVAL;
	default: EXIT(1);
	}
}


#endif

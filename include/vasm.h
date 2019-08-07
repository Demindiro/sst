#ifndef VASM_H
#define VASM_H



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
#define VASM_OP_CALL        1
#define VASM_OP_RET         2
#define VASM_OP_LOAD        3
#define VASM_OP_STORE       4
#define VASM_OP_PUSH        5
#define VASM_OP_POP         6
#define VASM_OP_MOV         7
#define VASM_OP_SET         8
#define VASM_OP_ADD         9
#define VASM_OP_SUB        10
#define VASM_OP_MUL        11
#define VASM_OP_DIV        12
#define VASM_OP_MOD       113
#define VASM_OP_REM       114
#define VASM_OP_LSHIFT     13
#define VASM_OP_RSHIFT     14
#define VASM_OP_LROT       15
#define VASM_OP_RROT       16
#define VASM_OP_AND        17
#define VASM_OP_OR         18
#define VASM_OP_XOR        19
#define VASM_OP_NOT       120
#define VASM_OP_INV       121
#define VASM_OP_SYSCALL    20
#define VASM_OP_JMP        21
#define VASM_OP_JZ         22
#define VASM_OP_JNZ        23
#define VASM_OP_JP         24
#define VASM_OP_JPZ        25


struct vasm {
	short op;
	char _[14];
};

struct vasm_str {
	short op;
	char *str;
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
	char *str;
};

struct vasm_reg2_str {
	short op;
	char  r[2];
	char *str;
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
	char  *lbl;
	size_t pos;
};


#endif

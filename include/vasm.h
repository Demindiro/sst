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

#define VASM_OP_JMP         1
#define VASM_OP_JZ          2
#define VASM_OP_JNZ         3
#define VASM_OP_JP          4
#define VASM_OP_JPZ         5
#define VASM_OP_CALL        6
#define VASM_OP_RET         7

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
#define VASM_OP_SET        33

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


struct lblmap {
	struct lblpos lbl2pos[4096];
	size_t lbl2poscount;
	struct lblpos pos2lbl[4096];
	size_t pos2lblcount;
};


#endif

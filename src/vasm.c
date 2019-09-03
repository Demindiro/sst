#include "vasm.h"
#include <string.h>
#include "util.h"


int get_vasm_args_type(int op)
{
	switch (op) {
	case OP_SYSCALL:
	case OP_RET:
	case OP_NOP:
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
	case OP_JMPRB:
		return ARGS_TYPE_BYTE;
	case OP_JMP:
	case OP_CALL:
		return ARGS_TYPE_LONG;
	case OP_JZB:
	case OP_JNZB:
	case OP_JPB:
	case OP_JPZB:
	case OP_SETB:
		return ARGS_TYPE_REGBYTE;
	case OP_SETS:
		return ARGS_TYPE_REGSHORT;
	case OP_SETI:
		return ARGS_TYPE_REGINT;
	case OP_JZ:
	case OP_JNZ:
	case OP_JP:
	case OP_JPZ:
	case OP_SETL:
	case OP_SET:
		return ARGS_TYPE_REGLONG;
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
		return -1;
		EXIT(1, "Undefined OP (%d)", op);
	}
}



int vasm2str(union vasm_all a, char *buf, size_t bufsize) {
	const char *op;
	switch (a.op) {
	case OP_NONE:
		buf[0] = 0;
		break;
	case OP_COMMENT:
		snprintf(buf, bufsize, "# %s", a.s.s);
		return 0;
	case OP_LABEL:
		snprintf(buf, bufsize, "%s:", a.s.s);
		return 0;
	case OP_NOP   : op = "nop"   ; break;
	case OP_CALL  : op = "call"  ; break;
	case OP_RET   : op = "ret"   ; break;
	case OP_JMP   : op = "jmp"   ; break;
	case OP_JMPRB : op = "jmprb" ; break;
	case OP_JZ    : op = "jz"    ; break;
	case OP_JNZ   : op = "jnz"   ; break;
	case OP_JZB   : op = "jzb"   ; break;
	case OP_JNZB  : op = "jnzb"  ; break;
	case OP_SET   : op = "set"   ; break;
	case OP_SETB  : op = "setb"  ; break;
	case OP_SETS  : op = "sets"  ; break;
	case OP_SETI  : op = "seti"  ; break;
	case OP_SETL  : op = "setl"  ; break;
	case OP_MOV   : op = "mov"   ; break;
	case OP_STRL  : op = "strl"  ; break;
	case OP_STRI  : op = "stri"  ; break;
	case OP_STRS  : op = "strs"  ; break;
	case OP_STRB  : op = "strb"  ; break;
	case OP_LDL   : op = "ldl"   ; break;
	case OP_LDI   : op = "ldi"   ; break;
	case OP_LDS   : op = "lds"   ; break;
	case OP_LDB   : op = "ldb"   ; break;
	case OP_STRBAT: op = "strbat"; break;
	case OP_STRSAT: op = "strsat"; break;
	case OP_STRIAT: op = "striat"; break;
	case OP_STRLAT: op = "strlat"; break;
	case OP_LDBAT : op = "ldbat" ; break;
	case OP_LDSAT : op = "ldsat" ; break;
	case OP_LDIAT : op = "ldiat" ; break;
	case OP_LDLAT : op = "ldlat" ; break;
	case OP_PUSH  : op = "push"  ; break;
	case OP_POP   : op = "pop"   ; break;
	case OP_ADD   : op = "add"   ; break;
	case OP_SUB   : op = "sub"   ; break;
	case OP_MUL   : op = "mul"   ; break;
	case OP_DIV   : op = "div"   ; break;
	case OP_REM   : op = "rem"   ; break;
	case OP_MOD   : op = "mod"   ; break;
	case OP_NOT   : op = "not"   ; break;
	case OP_INV   : op = "inv"   ; break;
	case OP_RSHIFT: op = "rshift"; break;
	case OP_LSHIFT: op = "lshift"; break;
	case OP_XOR   : op = "xor"   ; break;
	case OP_LESS  : op = "less"  ; break;
	case OP_LESSE : op = "lesse" ; break;
	case OP_SYSCALL:op ="syscall"; break;
	case OP_RAW_LONG:
		snprintf(buf, bufsize, ".long\t%s", a.s.s);
		return 0;
	case OP_RAW_STR:
		snprintf(buf, bufsize, ".str\t\"%s\"", a.s.s);
		return 0;
	default:
		snprintf(buf, bufsize, "???");
		return -1;
		EXIT(3, "Unknown OP (%d)", a.op);
	}

	switch (get_vasm_args_type(a.op)) {
	case ARGS_TYPE_NONE:
		snprintf(buf, bufsize, "%s", op);
		break;
	case ARGS_TYPE_REG1:
		snprintf(buf, bufsize, "%s\tr%d", op, a.r.r);
		break;
	case ARGS_TYPE_REG2:
		snprintf(buf, bufsize, "%s\tr%d,r%d", op, a.r2.r0, a.r2.r1);
		break;
	case ARGS_TYPE_REG3:
		snprintf(buf, bufsize, "%s\tr%d,r%d,r%d", op, a.r3.r0, a.r3.r1, a.r3.r2);
		break;
	case ARGS_TYPE_BYTE:
	case ARGS_TYPE_SHORT:
	case ARGS_TYPE_INT:
	case ARGS_TYPE_LONG:
		snprintf(buf, bufsize, "%s\t%s", op, a.s.s);
		break;
	case ARGS_TYPE_REGBYTE:
	case ARGS_TYPE_REGSHORT:
	case ARGS_TYPE_REGINT:
	case ARGS_TYPE_REGLONG:
		snprintf(buf, bufsize, "%s\tr%d,%s", op, a.rs.r, a.rs.s);
		break;
	default:
		EXIT(3, "OP arguments type not classified (%d)", a.op);
	}

	return 0;
}

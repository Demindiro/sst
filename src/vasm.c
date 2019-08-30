#include "vasm.h"
#include <string.h>
#include "util.h"

int vasm2str(union vasm_all a, char *buf, size_t bufsize, int indent) {
	if (indent) {
		*buf++ = '\t';
		bufsize--;
	}
	switch (a.op) {
	default:
		ERROR("Unknown OP (%d)", a.op);
		EXIT(1);
	case VASM_OP_NONE:
		buf[0] = 0;
		break;
	case VASM_OP_COMMENT:
		snprintf(buf, bufsize, "# %s", a.s.str);
		break;
	case VASM_OP_LABEL:
		snprintf(buf, bufsize, "%s:", a.s.str);
		break;
	case VASM_OP_NOP:
		snprintf(buf, bufsize, "nop");
		break;
	case VASM_OP_CALL:
		snprintf(buf, bufsize, "call\t%s", a.s.str);
		break;
	case VASM_OP_RET:
		snprintf(buf, bufsize, "ret");
		break;
	case VASM_OP_JMP:
		snprintf(buf, bufsize, "jmp\t%s", a.s.str);
		break;
	case VASM_OP_JZ:
		snprintf(buf, bufsize, "jz\t%s,r%d", a.rs.str, a.rs.r);
		break;
	case VASM_OP_JNZ:
		snprintf(buf, bufsize, "jnz\t%s,r%d", a.rs.str, a.rs.r);
		break;
	case VASM_OP_SET:
		snprintf(buf, bufsize, "set\tr%d,%s", a.rs.r, a.rs.str);
		break;
	case VASM_OP_MOV:
		snprintf(buf, bufsize, "mov\tr%d,r%d", a.r2.r[0], a.r2.r[1]);
		break;
	case VASM_OP_STRLAT:
		snprintf(buf, bufsize, "strlat\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_STRIAT:
		snprintf(buf, bufsize, "striat\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_STRSAT:
		snprintf(buf, bufsize, "strsat\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_STRBAT:
		snprintf(buf, bufsize, "strbat\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_LDLAT:
		snprintf(buf, bufsize, "ldlat\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_LDIAT:
		snprintf(buf, bufsize, "ldiat\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_LDSAT:
		snprintf(buf, bufsize, "ldsat\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_LDBAT:
		snprintf(buf, bufsize, "ldbat\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_PUSH:
		snprintf(buf, bufsize, "push\tr%d", a.r.r);
		break;
	case VASM_OP_POP:
		snprintf(buf, bufsize, "pop\tr%d", a.r.r);
		break;
	case VASM_OP_ADD:
		snprintf(buf, bufsize, "add\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_SUB:
		snprintf(buf, bufsize, "sub\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_MUL:
		snprintf(buf, bufsize, "mul\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_DIV:
		snprintf(buf, bufsize, "div\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_MOD:
		snprintf(buf, bufsize, "mod\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_NOT:
		snprintf(buf, bufsize, "not\tr%d,r%d", a.r2.r[0], a.r2.r[1]);
		break;
	case VASM_OP_INV:
		snprintf(buf, bufsize, "inv\tr%d,r%d", a.r2.r[0], a.r2.r[1]);
		break;
	case VASM_OP_RSHIFT:
		snprintf(buf, bufsize, "rshift\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_LSHIFT:
		snprintf(buf, bufsize, "lshift\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_XOR:
		snprintf(buf, bufsize, "xor\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_LESS:
		snprintf(buf, bufsize, "less\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	case VASM_OP_LESSE:
		snprintf(buf, bufsize, "lesse\tr%d,r%d,r%d", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
		break;
	}

	return 0;
}

#include "vasm.h"

int vasm2str(union vasm_all a, char *buf, size_t bufsize) {
	for (size_t h = 0; h < funccount; h++) {
		for (size_t i = 0; i < vasmcount[h]; i++) {
			union vasm_all a = vasms[h][i];
			switch (a.a.op) {
			default:
				ERROR("Unknown OP (%d)", vasms[h][i].op);
				abort();
			case VASM_OP_NONE:
				teeprintf("\n");
				break;
			case VASM_OP_COMMENT:
				for (size_t j = 0; j < vasmcount[h]; j++) {
					if (vasms[h][j].op == VASM_OP_NONE || vasms[h][j].op == VASM_OP_COMMENT)
						continue;
					if (vasms[h][j].op == VASM_OP_LABEL)
						teeprintf("# %s\n", a.s.str);
					else
						teeprintf("\t# %s\n", a.s.str);
				}
				break;
			case VASM_OP_LABEL:
				teeprintf("%s:\n", a.s.str);
				break;
			case VASM_OP_NOP:
				teeprintf("\tnop\n");
				break;
			case VASM_OP_CALL:
				teeprintf("\tcall\t%s\n", a.s.str);
				break;
			case VASM_OP_RET:
				teeprintf("\tret\n");
				break;
			case VASM_OP_JMP:
				teeprintf("\tjmp\t%s\n", a.s.str);
				break;
			case VASM_OP_JZ:
				teeprintf("\tjz\t%s,r%d\n", a.rs.str, a.rs.r);
				break;
			case VASM_OP_JNZ:
				teeprintf("\tjnz\t%s,r%d\n", a.rs.str, a.rs.r);
				break;
			case VASM_OP_SET:
				teeprintf("\tset\tr%d,%s\n", a.rs.r, a.rs.str);
				break;
			case VASM_OP_MOV:
				teeprintf("\tmov\tr%d,r%d\n", a.r2.r[0], a.r2.r[1]);
				break;
			case VASM_OP_STRLAT:
				teeprintf("\tstrlat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_STRIAT:
				teeprintf("\tstriat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_STRSAT:
				teeprintf("\tstrsat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_STRBAT:
				teeprintf("\tstrbat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LDLAT:
				teeprintf("\tldlat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LDIAT:
				teeprintf("\tldiat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LDSAT:
				teeprintf("\tldsat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LDBAT:
				teeprintf("\tldbat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_PUSH:
				teeprintf("\tpush\tr%d\n", a.r.r);
				break;
			case VASM_OP_POP:
				teeprintf("\tpop\tr%d\n", a.r.r);
				break;
			case VASM_OP_ADD:
				teeprintf("\tadd\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_SUB:
				teeprintf("\tsub\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_MUL:
				teeprintf("\tmul\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_DIV:
				teeprintf("\tdiv\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_MOD:
				teeprintf("\tmod\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_NOT:
				teeprintf("\tnot\tr%d,r%d\n", a.r2.r[0], a.r2.r[1]);
				break;
			case VASM_OP_INV:
				teeprintf("\tinv\tr%d,r%d\n", a.r2.r[0], a.r2.r[1]);
				break;
			case VASM_OP_RSHIFT:
				teeprintf("\trshift\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LSHIFT:
				teeprintf("\tlshift\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_XOR:
				teeprintf("\txor\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LESS:
				teeprintf("\tless\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LESSE:
				teeprintf("\tlesse\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			}
		}
		teeprintf("\n");
		for (size_t i = 0; i < stringcount; i++) {
			teeprintf(".long %lu\n_str_%lu:\t.str \"%s\"\n",
				  strlen(strings[i]), i, strings[i]);
		}
		printf("\n");
	}
}

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "func.h"
#include "lines.h"
#include "text2lines.h"
#include "lines2func.h"
#include "func2vasm.h"
#include "hashtbl.h"
#include "util.h"
#include "optimize/lines.h"
#include "optimize/vasm.h"


static const char *mathop2str(int op)
{
	switch (op) {
	case MATH_ADD:    return "+";
	case MATH_SUB:    return "-";
	case MATH_MUL:    return "*";
	case MATH_DIV:    return "/";
	case MATH_MOD:    return "%";
	//case MATH_NOT:    return "~";
	//case MATH_INV:    return "!";
	case MATH_RSHIFT: return ">>";
	case MATH_LSHIFT: return "<<";
	case MATH_XOR:    return "^";
	case MATH_LESS:   return "<";
	case MATH_LESSE:  return "<=";
	default: fprintf(stderr, "Invalid op (%d)\n", op); abort();
	}
}


static int _text2lines(char *buf,
                       line_t **lines , size_t *linecount,
		       char ***strings, size_t *stringcount)
{
	printf("=== text2lines ===\n");
	text2lines(buf, lines, linecount, strings, stringcount);
	printf("\n");
	for (size_t i = 0; i < *stringcount; i++)
		printf(".str_%lu = \"%s\"\n", i, (*strings)[i]);
	printf("\n");
	for (size_t i = 0; i < *linecount; i++)
		printf("%4lu | %s\n", i + 1, (*lines)[i].text);
	printf("\n");
	return 0;
}



static void _printfunc(struct func *f)
{
	printf("Name:   %s\n", f->name);
	printf("Return: %s\n", f->type);
	printf("Args:   %d\n", f->argcount);
	for (size_t j = 0; j < f->argcount; j++)
		printf("  %s -> %s\n", f->args[j].name, f->args[j].type);
	printf("Lines:  %d\n", f->linecount);
	for (size_t j = 0; j < f->linecount; j++) {
		union  func_line_all_p   fl = { .line = f->lines[j] } ;
		struct func_line_func   *flf;
		struct func_line_goto   *flg;
		struct func_line_if     *fli;
		struct func_line_label  *fll;
		struct func_line_math   *flm;
		struct func_line_return *flr;
		switch (f->lines[j]->type) {
		case FUNC_LINE_ASSIGN:
			if (fl.a->cons)
				printf("  Assign: %s = %s (const)\n", fl.a->var, fl.a->value);
			else
				printf("  Assign: %s = %s\n", fl.a->var, fl.a->value);
			break;
		case FUNC_LINE_DECLARE:
			printf("  Declare: %s\n", fl.d->var);
			break;
		case FUNC_LINE_DESTROY:
			printf("  Destroy: %s\n", fl.d->var);
			break;
		case FUNC_LINE_FUNC:
			flf = (struct func_line_func *)f->lines[j];
			printf("  Function:");
			if (flf->var != NULL)
				printf(" %s =", flf->var);
			printf(" %s", flf->name);
			if (flf->paramcount > 0)
				printf(" %s", flf->params[0]);
			for (size_t k = 1; k < flf->paramcount; k++)
				printf(",%s", flf->params[k]);
			printf("\n");
			break;
		case FUNC_LINE_GOTO:
			flg = (struct func_line_goto *)f->lines[j];
			printf("  Goto: %s\n", flg->label);
			break;
		case FUNC_LINE_IF:
			fli = (struct func_line_if *)f->lines[j];
			if (fli->inv)
				printf("  If not %s then %s\n", fli->var, fli->label);
			else
				printf("  If %s then %s\n", fli->var, fli->label);
			break;
		case FUNC_LINE_LABEL:
			fll = (struct func_line_label *)f->lines[j];
			printf("  Label: %s\n", fll->label);
			break;
		case FUNC_LINE_MATH:
			flm = (struct func_line_math *)f->lines[j];
			if (flm->op == MATH_INV)
				printf("  Math: %s = !%s\n", flm->x, flm->y);
			else if (flm->op == MATH_NOT)
				printf("  Math: %s = ~%s\n", flm->x, flm->y);
			else if (flm->op == MATH_LOADAT)
				printf("  Math: %s = %s[%s]\n", flm->x, flm->y, flm->z);
			else
				printf("  Math: %s = %s %s %s\n", flm->x, flm->y, mathop2str(flm->op), flm->z);
			break;
		case FUNC_LINE_RETURN:
			flr = (struct func_line_return *)f->lines[j];
			printf("  Return: %s\n", flr->val);
			break;
		default:
			printf("  Unknown line type (%d)\n", f->lines[j]->type);
			abort();
		}
	}
}



static int _lines2funcs(const line_t *lines , size_t linecount,
		        struct func **funcs, size_t *funccount,
		        struct hashtbl *functbl)
{
	size_t fc = 0, fs = 16;
	struct func *f = malloc(fs * sizeof *f);
	h_create(functbl, 4);
	printf("=== lines2funcs ===\n");
	for (size_t i = 0; i < linecount; i++) {
		line_t line = lines[i];
		if (strncmp(line.text, "extern ", sizeof "extern") == 0) {
			struct func *g = &f[fc++];
			parsefunc_header(g, line);
			if (g == NULL)
				return -1;
			h_add(functbl, g->name, (size_t)g);
		} else {
			struct func *g = &f[fc++];
			parsefunc_header(g, line);
			i++;
			size_t nestlvl = 1, nestlines[256];
			size_t j = i;
			for ( ; j < linecount; j++) {
				if (nestlvl == 0)
					break;
				const char *t = lines[j].text;
				if (streq(t, "end")) {
					nestlvl--;
				} else if (
				    strstart(t, "for "  ) ||
				    strstart(t, "if "   ) ||
				    strstart(t, "while ")) {
					nestlines[nestlvl] = j;
					nestlvl++;
				}
			}
			if (nestlvl > 0) {
				ERROR("Missing %lu 'end' statements", nestlvl);
				for (size_t k = 0; k < nestlvl; k++) {
					size_t l = nestlines[k];
					ERROR("%4lu | %s", lines[l].row, lines[l].text);
				}	
				return -1;
			}
			lines2func(lines + i, j - i + 1, g, functbl);
			optimizefunc(g);
			i = j;
		}
	}
	printf("\n");
	for (size_t i = 0; i < fc; i++)
		_printfunc(&f[i]);
	printf("\n");
	*funccount = fc;
	*funcs     = realloc(f, fc * sizeof *f);
	return 0;
}


int main(int argc, char **argv)
{	
	if (argc < 3) {
		ERROR("Usage: %s <input> <output>", argv[0]);
		return 1;
	}

	// ALL THE WAAAY
	optimize_lines_options = -1;
	//optimize_lines_options = 0;

	char  **strings;
	line_t *lines;
	size_t stringcount, linecount;
	struct func *funcs;
	size_t funccount;
	struct hashtbl functbl;

	// Read source
	char buf[0x10000];
	int fd = open(argv[1], O_RDONLY);
	read(fd, buf, sizeof buf);
	close(fd);

	if (_text2lines(buf, &lines, &linecount, &strings, &stringcount) < 0) {
		ERROR("Failed text to lines stage");
		return 1;
	}

	if (_lines2funcs(lines, linecount, &funcs, &funccount, &functbl) < 0) {
		ERROR("Failed lines to functions stage");
		return 1;
	}


	union vasm_all **vasms = malloc(funccount * sizeof *vasms);
	size_t *vasmcount = malloc(funccount * sizeof *vasmcount);
	printf("=== structs2vasm ===\n");
	for (size_t i = 0; i < funccount; i++) {
		func2vasm(&vasms[i], &vasmcount[i], &funcs[i]);
		optimizevasm(vasms[i], &vasmcount[i]);
	}
	printf("\n");
	FILE *_f = fopen(argv[2], "w");
	#define teeprintf(...) do {             \
		fprintf(stderr, ##__VA_ARGS__); \
		fprintf(_f, ##__VA_ARGS__);     \
	} while (0)
	for (size_t h = 0; h < funccount; h++) {
		for (size_t i = 0; i < vasmcount[h]; i++) {
			union vasm_all a = vasms[h][i];
			switch (a.a.op) {
			default:
				fprintf(stderr, "Unknown OP (%d)\n", vasms[h][i].op);
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
			case VASM_OP_STORELAT:
				teeprintf("\tstoreat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LOADLAT:
				teeprintf("\tloadlat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
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
			teeprintf(".str_%lu:\n"
				  "\t.long %lu\n"
				  "\t.str \"%s\"\n",
				  i, strlen(strings[i]), strings[i]);
		}
		printf("\n");
	}

	return 0;
}

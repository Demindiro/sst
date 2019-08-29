#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
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



struct linerange {
	struct func  *func;
	const line_t *lines;
	size_t        count;
};



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


static int _text2lines(const char *buf,
                       line_t **lines , size_t *linecount,
		       char ***strings, size_t *stringcount)
{
	DEBUG("Converting text to lines");
	text2lines(buf, lines, linecount, strings, stringcount);
	line_t *l = *lines;
	for (size_t i = 0; i < *stringcount; i++)
		DEBUG(".str_%lu = \"%s\"", i, (*strings)[i]);
	for (size_t i = 0; i < *linecount; i++)
		DEBUG("%4u:%-2u (%6lu) | %s", l[i].pos.y, l[i].pos.x, l[i].pos.c, l[i].text);
	return 0;
}



static void _printfunc(struct func *f)
{
	DEBUG("Name:   %s", f->name);
	DEBUG("Return: %s", f->type);
	DEBUG("Args:   %d", f->argcount);
	for (size_t j = 0; j < f->argcount; j++)
		DEBUG("  %s -> %s", f->args[j].name, f->args[j].type);
	DEBUG("Lines:  %d", f->linecount);
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
				DEBUG("  Assign: %s = %s (const)", fl.a->var, fl.a->value);
			else
				DEBUG("  Assign: %s = %s", fl.a->var, fl.a->value);
			break;
		case FUNC_LINE_DECLARE:
			DEBUG("  Declare: %s %s", fl.d->type, fl.d->var);
			break;
		case FUNC_LINE_DESTROY:
			DEBUG("  Destroy: %s", fl.d->var);
			break;
		case FUNC_LINE_FUNC:
			flf = (struct func_line_func *)f->lines[j];
#ifndef NDEBUG
			fprintf(stderr, "DEBUG:   Function:");
#else
			fprintf(stderr, "  Function:");
#endif
			if (flf->var != NULL)
				fprintf(stderr, " %s =", flf->var);
			fprintf(stderr, " %s", flf->name);
			if (flf->argcount > 0)
				fprintf(stderr, " %s", flf->args[0]);
			for (size_t k = 1; k < flf->argcount; k++)
				fprintf(stderr, ",%s", flf->args[k]);
			fprintf(stderr, "\n");
			break;
		case FUNC_LINE_GOTO:
			flg = (struct func_line_goto *)f->lines[j];
			DEBUG("  Goto: %s", flg->label);
			break;
		case FUNC_LINE_IF:
			fli = (struct func_line_if *)f->lines[j];
			if (fli->inv)
				DEBUG("  If not %s then %s", fli->var, fli->label);
			else
				DEBUG("  If %s then %s", fli->var, fli->label);
			break;
		case FUNC_LINE_LABEL:
			fll = (struct func_line_label *)f->lines[j];
			DEBUG("  Label: %s", fll->label);
			break;
		case FUNC_LINE_MATH:
			flm = (struct func_line_math *)f->lines[j];
			if (flm->op == MATH_INV)
				DEBUG("  Math: %s = !%s", flm->x, flm->y);
			else if (flm->op == MATH_NOT)
				DEBUG("  Math: %s = ~%s", flm->x, flm->y);
			else if (flm->op == MATH_LOADAT)
				DEBUG("  Math: %s = %s[%s]", flm->x, flm->y, flm->z);
			else
				DEBUG("  Math: %s = %s %s %s", flm->x, flm->y, mathop2str(flm->op), flm->z);
			break;
		case FUNC_LINE_RETURN:
			flr = (struct func_line_return *)f->lines[j];
			DEBUG("  Return: %s", flr->val);
			break;
		case FUNC_LINE_STORE:
			DEBUG("  Store: %s[%s] = %s", fl.s->var, fl.s->index, fl.s->val);
			break;
		default:
			DEBUG("  Unknown line type (%d)", f->lines[j]->type);
			abort();
		}
	}
	DEBUG("");
}


static size_t _find_func_length(const line_t *lines, size_t linecount, const char *text)
{
	DEBUG("Searching for function boundaries");
	size_t nestlvl = 1, nestlines[256];
	size_t i = 0;
	for ( ; i < linecount; i++) {
		if (nestlvl == 0)
			break;
		const char *t = lines[i].text;
		if (streq(t, "end")) {
			nestlvl--;
		} else if (
		    strstart(t, "for "  ) ||
		    strstart(t, "if "   ) ||
		    strstart(t, "while ")) {
			nestlines[nestlvl] = i;
			nestlvl++;
		}
	}
	if (nestlvl > 0) {
		ERROR("Missing %lu 'end' statements", nestlvl);
		for (size_t j = 0; j < nestlvl; j++) {
			size_t k = nestlines[j];
			PRINTLINE(lines[k]);
		}	
		EXIT(1);
	}
	return i;
}


static void _include(const char *f, struct hashtbl *functbl,
                     struct linerange **funclns, size_t *funclncount,
		     struct hashtbl *incltbl);


static void _findboundaries(const line_t *lines, size_t linecount,
			    struct hashtbl *functbl,
                            struct linerange **funclns, size_t *funclncount,
			    struct hashtbl *incltbl,
                            const char *text)
{
	h_create(functbl, 4);
	DEBUG("Parsing lines to functions");
	DEBUG("Searching for function boundaries");
	for (size_t i = 0; i < linecount; i++) {
		line_t line = lines[i];
		if (strstart(line.text, "extern ")) {
			struct func *g = malloc(sizeof *g);
			parsefunc_header(g, line, text);
			DEBUG("Adding external function '%s'", g->name);
			h_add(functbl, g->name, (size_t)g);
		} else if (strstart(line.text, "include ")) {
			char f[256];
			strncpy(f, line.text + strlen("include "), sizeof f); 
			_include(f, functbl, funclns, funclncount, incltbl);
		} else {
			struct func *g = malloc(sizeof *g);
			parsefunc_header(g, line, text);
			DEBUG("Adding function '%s'", g->name);
			h_add(functbl, g->name, (size_t)g);
			i++;
			size_t l = _find_func_length(lines + i, linecount - i, text);
			(*funclns)[*funclncount].func  = g;
			(*funclns)[*funclncount].lines = lines + i;
			(*funclns)[*funclncount].count = l;
			(*funclncount)++;
			i += l - 1;
		}
	}
}


static void _include(const char *f, struct hashtbl *functbl,
                     struct linerange **funclns, size_t *funclncount,
		     struct hashtbl *incltbl)
{
	DEBUG("Including %s", f);
	char buf[0x10000];
	char *b = buf;
	for (const char *c = f; *c != 0; b++, c++) {
		if (*c == '.')
			*b = '/';
		else
			*b = *c;
	}
	strcpy(b, ".sst");
	char cwd[4096];
	getcwd(cwd, sizeof cwd);
	chdir("lib"); // TODO
	int fd = open(buf, O_RDONLY);
	if (fd == -1) {
		ERROR("Couldn't open '%s': %s", buf, strerror(errno));
		EXIT(1);
	}
	read(fd, buf, sizeof buf);
	close(fd);
	chdir(cwd);

	char  **strings;
	line_t *lines;
	size_t stringcount, linecount;

	if (_text2lines(buf, &lines, &linecount, &strings, &stringcount) < 0) {
		ERROR("Failed text to lines stage");
		EXIT(1);
	}

	_findboundaries(lines, linecount, functbl,
			funclns, funclncount, incltbl, buf);
}


static void _lines2funcs(const line_t *lines, size_t linecount,
                         struct func **funcs, size_t *funccount,
                         struct hashtbl *functbl, const char *text)
{
	h_create(functbl, 4);
	struct linerange funclns[1024];
	size_t funclnc = 0;
	DEBUG("Parsing lines to functions");
	struct hashtbl incltbl;
	h_create(&incltbl, 4);
	struct linerange *_f = funclns;
	_findboundaries(lines, linecount, functbl,
	                &_f, &funclnc,
			&incltbl, text);
	DEBUG("%lu functions to be parsed", funclnc);
	DEBUG("Parsing lines between function boundaries");
	for (size_t i = 0; i < funclnc; i++) {
		struct linerange l = funclns[i];
		DEBUG("  Parsing '%s'", l.func->name);
		lines2func(l.lines, l.count, l.func, functbl);
		optimizefunc(l.func);
	}
	for (size_t i = 0; i < funclnc; i++)
		_printfunc(funclns[i].func);
	*funccount = funclnc;
	*funcs     = malloc(funclnc * sizeof **funcs);
	for (size_t i = 0; i < funclnc; i++)
		(*funcs)[i] = *funclns[i].func;
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

	_lines2funcs(lines, linecount, &funcs, &funccount, &functbl, buf);

	union vasm_all **vasms = malloc(funccount * sizeof *vasms);
	size_t *vasmcount = malloc(funccount * sizeof *vasmcount);
	DEBUG("Converting functions to assembly");
	for (size_t i = 0; i < funccount; i++) {
		DEBUG("  Converting '%s'", funcs[i].name);
		func2vasm(&vasms[i], &vasmcount[i], &funcs[i]);
		optimizevasm(vasms[i], &vasmcount[i]);
	}
	DEBUG("");
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
			case VASM_OP_STORELAT:
				teeprintf("\tstorelat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_STOREIAT:
				teeprintf("\tstoreiat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_STORESAT:
				teeprintf("\tstoresat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_STOREBAT:
				teeprintf("\tstorebat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LOADLAT:
				teeprintf("\tloadlat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LOADIAT:
				teeprintf("\tloadiat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LOADSAT:
				teeprintf("\tloadsat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
				break;
			case VASM_OP_LOADBAT:
				teeprintf("\tloadbat\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
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

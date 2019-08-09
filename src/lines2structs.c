#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "hashtbl.h"
#include "macro.h"


#define FUNC_LINE_NONE   0
#define FUNC_LINE_ASSIGN 1
#define FUNC_LINE_FUNC   2
#define FUNC_LINE_GOTO   3
#define FUNC_LINE_IF     4
#define FUNC_LINE_LABEL  5
#define FUNC_LINE_MATH   6
#define FUNC_LINE_RETURN 7

#define MATH_ADD VASM_OP_ADD
#define MATH_SUB VASM_OP_SUB
#define MATH_MUL VASM_OP_MUL
#define MATH_DIV VASM_OP_DIV
#define MATH_MOD VASM_OP_MOD
#define MATH_NOT VASM_OP_NOT
#define MATH_INV VASM_OP_INV


//#define streq(x,y) (strcmp(x,y) == 0)
#define isnum(c) ('0' <= c && c <= '9')


struct variable {
	char type[32];
	char name[32];
};

struct func_line {
	char type;
};

struct func_line_assign {
	struct func_line line;
	char *var;
	char *value;
};

struct func_line_func {
	struct func_line line;
	unsigned char paramcount;
	struct variable  assign;
	char name[32];
	char params[32][32];
};

struct func_line_goto {
	struct func_line line;
	char *label;
};

struct func_line_if {
	struct func_line line;
	char *label;
	char *var;
};

struct func_line_label {
	struct func_line line;
	char *label;
};

struct func_line_math {
	struct func_line line;
	char op;
	char *x, *y, *z;
};

struct func_line_return {
	struct func_line line;
	char *val;
};

union func_line_all_p {
	struct func_line *line;
	struct func_line_assign *a;
	struct func_line_func   *f;
	struct func_line_goto   *g;
	struct func_line_if     *i;
	struct func_line_label  *l;
	struct func_line_math   *m;
	struct func_line_return *r;
};


struct func_arg {
	char type[32];
	char name[32];
};


struct func {
	char type[32];
	char name[32];
	unsigned char argcount;
	unsigned char linecount;
	struct func_arg   args[16];
	struct func_line *lines[256];
};


char *strings[4096];
size_t stringcount;

char *lines[4096];
size_t linecount;

struct func funcs[4096];
size_t funccount;



static char *strclone(const char *text) {
	size_t l = strlen(text);
	char *m = malloc(l + 1);
	if (m == NULL)
		return m;
	memcpy(m, text, l + 1);
	return m;
}



static const char *mathop2str(int op)
{
	switch (op) {
	case MATH_ADD: return "+";
	case MATH_SUB: return "-";
	case MATH_MUL: return "*";
	case MATH_DIV: return "/";
	case MATH_MOD: return "%";
	case MATH_INV: return "!";
	default: fprintf(stderr, "Invalid op (%d)\n", op); abort();
	}
}


static int mathop_precedence(int op)
{
	switch (op) {
	case '*':
	case '/':
	case '%':
		return 10;
	case '+':
	case '-':
		return 9;
	case '=':
	case '!':
		return 8;
	default:
		return 9999;
	}
}



static char *parseexpr(const char *p, struct func *f, size_t *k)
{
	static int varcounter = 0;
	char words[4][32];
	memset(words, 0, sizeof words);
	size_t i = 0;
	for ( ; i < 2; i++) {
		const char *s = p;
		while (*p != ' ' && *p != 0)
			p++;
		memcpy(words[i], s, p - s);
		words[i][p - s] = 0;
		if (*p == 0)
			break;
		p++;
	}
	const char *op = p;
	for ( ; i >= 2 && i < 4; i++) {
		const char *s = p;
		while (*p != ' ' && *p != 0)
			p++;
		memcpy(words[i], s, p - s);
		words[i][p - s] = 0;
		if (*p == 0)
			break;
		p++;
	}
	p = op;

	switch (i) {
	case 0:
		return strclone(words[0]);
	case 1:
		fprintf(stderr, "%d@%s: '%s' --> %d\n", __LINE__, __FILE__, p, i);
		abort();
	default: {
		char var[32];
		snprintf(var, sizeof var, "_var_%d", varcounter);
		varcounter++;
		char op;
		union func_line_all_p fl;
		switch (words[1][0]) {
		case '+':
			op = MATH_ADD;
			break;
		case '-':
			op = MATH_SUB;
			break;
		case '*':
			op = MATH_MUL;
			break;
		case '/':
			op = MATH_DIV;
			break;
		case '%':
			op = MATH_MOD;
			break;
		case '=':
		case '!':
			op = MATH_SUB; // TODO invert or smth
			break;
		default:
			fprintf(stderr, "Invalid math OP (%s)\n", words[1]);
			abort();
		}
		int precdl = mathop_precedence(words[1][0]);
		int precdr = mathop_precedence(words[3][0]);
		printf("%c --> %d\n", words[1][0], precdl);
		printf("%c --> %d\n", words[3][0], precdr);
		printf("---------\n");
		fl.m = calloc(sizeof *fl.m, 1);
		fl.m->line.type = FUNC_LINE_MATH;
		fl.m->op = op;
		char *x;
		if (precdl >= precdr) {
			fl.m->x = strclone(var);
			fl.m->y = strclone(words[0]);
			fl.m->z = strclone(words[2]);
			f->lines[*k] = fl.line;
			(*k)++;
			x = fl.m->x;
			// Dragons
			size_t ok = *k;
			char *v = fl.m->x;
			parseexpr(p, f, k);
			fl.line = f->lines[ok];
			fl.m->y = v;
		} else {
			fl.m->x = strclone(var);
			fl.m->y = strclone(words[0]);
			fl.m->z = parseexpr(p, f, k);
			f->lines[*k] = fl.line;
			(*k)++;
			x = fl.m->x;
		}
		if (words[1][0] == '=') { // meh
			printf("ayy\n");
			fl.m = calloc(sizeof *fl.m, 1);
			fl.m->line.type = FUNC_LINE_MATH;
			fl.m->op = MATH_INV;
			fl.m->x  = x;
			fl.m->y  = x;
			f->lines[*k] = fl.line;
			(*k)++;
		}
		return fl.m->x;
	}
	}
}


static int parsefunc(size_t start, size_t end) {

	size_t i = 0, j = 0;
	char *line = lines[start];
	struct func *f = &funcs[funccount];
	
	// Parse declaration
	// Get type
	while (line[j] != ' ')
		j++;
	memcpy(f->type, line + i, j - i);

	// Skip whitespace
	while (line[j] == ' ')
		j++;
	i = j;

	// Get name
	while (line[j] != ' ' && line[j] != '(')
		j++;
	memcpy(f->name, line + i, j - i);

	// Skip whitespace
	while (line[j] == ' ')
		j++;
	i = j;

	// Parse arguments
	if (line[j] != '(')
		abort();
	j++;
	size_t k = 0;
	for ( ; ; ) {
		// Argument list done
		if (line[j] == ')')
			break;
		// Skip whitespace
		while (line[j] == ' ')
			j++;
		i = j;
		// Get type
		while (line[j] != ' ')
			j++;
		memcpy(f->args[k].type, line + i, j - i);
		// Skip whitespace
		while (line[j] == ' ')
			j++;
		i = j;
		// Get type
		while (line[j] != ' ' && line[j] != ')')
			j++;
		memcpy(f->args[k].name, line + i, j - i);
		// Skip whitespace
		while (line[j] == ' ')
			j++;
	}
	f->argcount = k;

	// Parse lines
	start++;
	line = lines[start];
	k = 0;
	struct func_line_goto  *loopjmps[16];
	struct func_line_math  *formath[16];
	struct func_line_label *loopelse[16];
	struct func_line_label *loopends[16];
	char looptype[16];
	int  loopcounter = 0;
	int  loopcount   = 0;
	for ( ; ; ) {

		#define NEXTWORD do { \
			char *_ptr = ptr; \
			while (*ptr != ' ' && *ptr != 0) \
				ptr++; \
			memcpy(word, _ptr, ptr - _ptr); \
			word[ptr - _ptr] = 0; \
			if (*ptr != 0) \
				ptr++; \
		} while (0)

		// Parse first word
		char word[32];
		char *ptr = line;
		NEXTWORD;
		
		union func_line_all_p flp;
		// Determine line type
		if (streq(word, "break")) {
			size_t l = loopcount - 1;
			while (l >= 0 && looptype[l] == 3)
				l--;
			if (l >= 0) {
				flp.g = calloc(sizeof *flp.g, 1);
				flp.g->line.type = FUNC_LINE_GOTO;
				flp.g->label = loopends[l]->label;
				f->lines[k] = flp.line;
				k++;
			} else {
				fprintf(stderr, "'break' outside loop\n");
				abort();
			}
		} else if (streq(word, "end")) {
			if (loopcount > 0) {
				loopcount--;
				if (loopelse[loopcount] != NULL) {
					switch (looptype[loopcount]) {
					case 1:
						f->lines[k] = (struct func_line *)formath[loopcount];
						k++;
					case 2:
						f->lines[k] = (struct func_line *)loopjmps[loopcount];
						k++;
					case 3:
						f->lines[k] = (struct func_line *)loopelse[loopcount];
						k++;
						break;
					default:
						fprintf(stderr, "Invalid loop type (%d)\n", looptype[loopcount]);
						abort();
					}
				}
				f->lines[k] = (struct func_line *)loopends[loopcount];
				k++;
			} else {
				break;
			}
		} else if (streq(word, "else")) {
			if (loopcount > 0) {
				loopcount--;
				switch (looptype[loopcount]) {
				case 1:
					f->lines[k] = (struct func_line *)formath[loopcount];
					k++;
				case 2:
					f->lines[k] = (struct func_line *)loopjmps[loopcount];
					k++;
				case 3:
					f->lines[k] = (struct func_line *)loopelse[loopcount];
					loopelse[loopcount] = NULL;
					k++;
					break;
				default:
					fprintf(stderr, "Invalid loop type (%d)\n", looptype[loopcount]);
					abort();
				}
				loopcount++;
			} else {
				fprintf(stderr, "Unexpected 'else'\n");
				abort();
			}

		} else if (streq(word, "for")) {

			NEXTWORD;

			char *var = strclone(word);

			NEXTWORD;

			if (!streq(word, "in")) {
				fprintf(stderr, "Expected 'in', got '%s'\n", word);
				return -1;
			}

			char *p = ptr;
			while (strncmp(ptr, " to ", 4) != 0) {
				ptr++;
				if (*ptr == 0) {
					fprintf(stderr, "Expected 'to', got '%s'\n", p);
					return -1;
				}
			}
			memcpy(word, p, ptr - p);
			word[ptr - p] = 0;

			char *fromval = parseexpr(word, f, &k);

			NEXTWORD;
			NEXTWORD;

			char *toval = parseexpr(ptr, f, &k);

			struct func_line_assign *fla = calloc(sizeof *fla, 1);
			fla->line.type = FUNC_LINE_ASSIGN;
			fla->var       = var;
			fla->value     = fromval;
			f->lines[k]    = (struct func_line *)fla;
			k++;

			char lbl[16];
		       	snprintf(lbl, sizeof lbl, ".for_%d", loopcounter);

			struct func_line_label  *fll = calloc(sizeof *fll, 1);
			fll->line.type = FUNC_LINE_LABEL;
			fll->label     = strclone(lbl);
			f->lines[k]    = (struct func_line *)fll;
			k++;

			formath[loopcount] = calloc(sizeof *formath[loopcount], 1);
			formath[loopcount]->line.type = FUNC_LINE_MATH;
			formath[loopcount]->op       = MATH_ADD;
			formath[loopcount]->x        = fla->var;
			formath[loopcount]->y        = fla->var;
			formath[loopcount]->z        = "1";

			loopjmps[loopcount] = calloc(sizeof *loopjmps[loopcount], 1);
			loopjmps[loopcount]->line.type = FUNC_LINE_GOTO;
			loopjmps[loopcount]->label     = fll->label;

		       	snprintf(lbl, sizeof lbl, ".for_%d_else", loopcounter);
			loopelse[loopcount] = calloc(sizeof *loopelse[loopcount], 1);
			loopelse[loopcount]->line.type = FUNC_LINE_LABEL;
			loopelse[loopcount]->label     = strclone(lbl);

		       	snprintf(lbl, sizeof lbl, ".for_%d_end", loopcounter);
			loopends[loopcount] = calloc(sizeof *loopends[loopcount], 1);
			loopends[loopcount]->line.type = FUNC_LINE_LABEL;
			loopends[loopcount]->label     = strclone(lbl);

			struct func_line_if     *fli = calloc(sizeof *fli, 1);
			char expr[64];
			snprintf(expr, sizeof expr, "%s == %s", fla->var, toval);
			fli->line.type = FUNC_LINE_IF;
			fli->label     = loopelse[loopcount]->label;
			fli->var       = parseexpr(expr, f, &k);
			f->lines[k]    = (struct func_line *)fli;
			k++;

			looptype[loopcount] = 1;

			loopcount++;
			loopcounter++;
		} else if (streq(word, "while")) {
			char lbl[16], lbll[16], lble[16];
		       	snprintf(lbl , sizeof lbl , ".while_%d"     , loopcounter);
		       	snprintf(lbll, sizeof lbll, ".while_%d_else", loopcounter);
		       	snprintf(lble, sizeof lble, ".while_%d_end" , loopcounter);
			char *lblc  = strclone(lbl );
			char *lbllc = strclone(lbll);
			char *lblec = strclone(lble);
			
			flp.l = calloc(sizeof *flp.l, 1);
			flp.l->line.type = FUNC_LINE_LABEL;
			flp.l->label     = lblc;
			f->lines[k]      = flp.line;
			k++;

			char *v = parseexpr(ptr, f, &k);
			char expr[64];
			snprintf(expr, sizeof expr, "%s == 0", v);
			flp.i = calloc(sizeof *flp.i, 1);
			flp.i->line.type = FUNC_LINE_IF;
			flp.i->label     = lbllc;
			flp.i->var       = parseexpr(expr, f, &k);
			f->lines[k]      = flp.line;
			k++;

			flp.g = calloc(sizeof *flp.g, 1);
			flp.g->line.type    = FUNC_LINE_GOTO;
			flp.g->label        = lblc;
			loopjmps[loopcount] = flp.g;

			loopelse[loopcount] = calloc(sizeof *loopelse[loopcount], 1);
			loopelse[loopcount]->line.type = FUNC_LINE_LABEL;
			loopelse[loopcount]->label     = lbllc;

			loopends[loopcount] = calloc(sizeof *loopends[loopcount], 1);
			loopends[loopcount]->line.type = FUNC_LINE_LABEL;
			loopends[loopcount]->label     = lblec;

			looptype[loopcount] = 2;

			loopcount++;
			loopcounter++;
		} else if (streq(word, "if")) {
			char lbll[16], lble[16];
		       	snprintf(lbll, sizeof lbll, ".if_%d_else", loopcounter);
		       	snprintf(lble, sizeof lble, ".if_%d_end" , loopcounter);
			char *lbllc = strclone(lbll);
			char *lblec = strclone(lble);

			char *v = parseexpr(ptr, f, &k);
			char expr[64];
			snprintf(expr, sizeof expr, "%s == 0", v);

			flp.i = calloc(sizeof *flp.i, 1);
			flp.i->line.type = FUNC_LINE_IF;
			flp.i->var = parseexpr(expr, f, &k);
			flp.i->label = lbllc;
			f->lines[k]  = flp.line;
			k++;

			flp.l = calloc(sizeof *flp.l, 1);
			flp.l->line.type = FUNC_LINE_LABEL;
			flp.l->label = lblec;

			loopelse[loopcount] = calloc(sizeof *loopelse[loopcount], 1);
			loopelse[loopcount]->line.type = FUNC_LINE_LABEL;
			loopelse[loopcount]->label     = lbllc;

			loopends[loopcount] = calloc(sizeof *loopends[loopcount], 1);
			loopends[loopcount]->line.type = FUNC_LINE_LABEL;
			loopends[loopcount]->label     = lblec;

			looptype[loopcount] = 3;

			loopcount++;
			loopcounter++;
		} else if (streq(word, "return")) {
			struct func_line_return *flr = calloc(sizeof *flr, 1);
			flr->line.type = FUNC_LINE_RETURN;
			flr->val = strclone(ptr);
			f->lines[k] = (struct func_line *)flr;
			k++;
		} else {
			char name[32];
			strcpy(name, word);
			NEXTWORD;

			if (streq(word, "=")) {
				flp.a = calloc(sizeof *flp.a, 1);
				flp.a->line.type = FUNC_LINE_ASSIGN;
				flp.a->var       = strclone(name);
				flp.a->value     = strclone(ptr);
				f->lines[k]      = flp.line;
				k++;
			} else if (strchr("+-*/", word[0]) && word[1] == '=' &&
			           word[2] == 0) {
				flp.m = calloc(sizeof *flp.m, 1);
				flp.m->line.type = FUNC_LINE_MATH;
				flp.m->op        = MATH_ADD; // TODO
				flp.m->x         = strclone(name);
				flp.m->y         = flp.m->x;
				NEXTWORD;
				flp.m->z         = strclone(word);
				f->lines[k]      = flp.line;
				k++;
			} else {
				struct func_line_func *flf = calloc(sizeof *flf, 1);
				strcpy(flf->name, name);
				if (word[0] != 0) {
					strcpy(flf->params[0], word);
					flf->paramcount++;
					while (1) {
						NEXTWORD;
						if (word[0] == 0)
							break;
						strcpy(flf->params[flf->paramcount], word);
						flf->paramcount++;
					}
				}
				flf->line.type = FUNC_LINE_FUNC;
				f->lines[k] = (struct func_line *)flf;
				k++;
			}
		}

		// NEXT PLEASE!!!
		start++;
		line = lines[start];
	}
	f->linecount = k;

	return 0;
}


int lines2structs(void) {

	for (size_t i = 0; i < linecount; i++) {
		char *line = lines[i];
		size_t l = strlen(line);
		if (line[l - 1] == ')') {
			size_t start = i;
			// Find end of func
			while (strcmp(lines[i], "end") != 0)
				i++;
			parsefunc(start, i);
			funccount++;
		}
	}

	return 0;
}

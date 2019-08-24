/**
 * Steps:
 *   0. Parse text into lines
 *   1. Parse lines into structs
 *   2. Optimize structs
 *   3. Parse funcs into virt assembly
 *   4. Optimize virt assembly
 *   5. Convert virt assembly to real assembly
 *   6. Optimize real assembly
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "hashtbl.h"


#define FUNC_LINE_NONE    0
#define FUNC_LINE_ASSIGN  1
#define FUNC_LINE_DECLARE 2
#define FUNC_LINE_DESTROY 3
#define FUNC_LINE_FUNC    4
#define FUNC_LINE_GOTO    5
#define FUNC_LINE_IF      6
#define FUNC_LINE_LABEL   7
#define FUNC_LINE_LOAD    8
#define FUNC_LINE_MATH    9
#define FUNC_LINE_RETURN 10

#define MATH_ADD    VASM_OP_ADD
#define MATH_SUB    VASM_OP_SUB
#define MATH_MUL    VASM_OP_MUL
#define MATH_DIV    VASM_OP_DIV
#define MATH_MOD    VASM_OP_MOD
#define MATH_NOT    VASM_OP_NOT
#define MATH_INV    VASM_OP_INV
#define MATH_RSHIFT VASM_OP_RSHIFT
#define MATH_LSHIFT VASM_OP_LSHIFT
#define MATH_AND    VASM_OP_AND
#define MATH_OR     VASM_OP_OR
#define MATH_XOR    VASM_OP_XOR
#define MATH_LOADAT VASM_OP_LOADLAT
#define MATH_LESS   VASM_OP_LESS
#define MATH_LESSE  VASM_OP_LESSE


#define streq(x,y) (strcmp(x,y) == 0)
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
	char cons;
};

struct func_line_declare {
	struct func_line line;
	char *var;
};

struct func_line_func {
	struct func_line line;
	unsigned char paramcount;
	char name[32];
	char params[32][32];
	char *var;
};

struct func_line_goto {
	struct func_line line;
	char *label;
};

struct func_line_if {
	struct func_line line;
	char *label;
	char *var;
	char inv;
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
	struct func_line_assign  *a;
	struct func_line_declare *d;
	struct func_line_func    *f;
	struct func_line_goto    *g;
	struct func_line_if      *i;
	struct func_line_label   *l;
	struct func_line_math    *m;
	struct func_line_return  *r;
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

struct hashtbl functbl;

struct func funcs[4096];
size_t funccount;

struct vasm vasms[4096];
size_t vasmcount;

char vbin[0x10000];
size_t vbinlen;




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



static int text2lines(char *text) {
	char *c = text;
	while (1) {
		// Skip whitespace
		while (*c == '\n' || *c == ';' || *c == ' ' || *c == '\t')
			c++;
		// EOL
		if (*c == 0)
			break;
		// Comment
		if (*c == '#') {
			while (*c != '\n' && *c != 0)
				c++;
			continue;
		}
		// Copy line
		char buf[256], *ptr = buf;

		// Check if declaration + assignment
		char *d = c;
		while (*c != ' ' && *c != '\t' && *c != '\n')
			c++;
		char b[64];
		size_t l = c - d;
		memcpy(b, d, l);
		b[l] = 0;
		if (streq(b, "long") || streq(b, "char[]")) {
			b[l] = ' ';
			l++;
			d = c + 1;
			while (*d == ' ' || *d == '\t')
				d++;
			c = d;
			while (*d != ' ' && *d != '\t' && *d != '\n') {
				b[l] = *d;
				l++, d++;
			}
			b[l] = 0;
			lines[linecount] = strclone(b);
			linecount++;
		} else {
			c = d;
		}
		while (*c != '\n' && *c != ';') {
			if (*c == ' ' || *c == '\t') {
				*ptr = ' ';
				ptr++;
				while (*c == ' ' || *c == '\t')
					c++;
				*ptr = *c;
			} else {
				*ptr = *c;
				ptr++, c++;
			}
			if (strncmp(c, "true", 4) == 0 && (c[4] == 0 || strchr("\n \t", c[4]))) {
				*ptr = '1';
				ptr += 1;
				c   += 4;
			} else if (strncmp(c, "false ", 5) == 0 && (c[5] == 0 || strchr("\n \t", c[5]))) {
				*ptr = '0';
				ptr += 1;
				c   += 5;
			} else if (*c == '"') {
				memcpy(ptr, ".str_", sizeof ".str_" - 1);
				ptr += sizeof ".str_" - 1;
				*ptr = '0' + stringcount;
				ptr++;
				char buf2[4096], *ptr2 = buf2;
				c++;
				while (*c != '"') {
					*ptr2 = *c;
					ptr2++, c++;
				}
				c++;
				char *p = malloc(ptr2 - buf2);
				memcpy(p, buf2, ptr2 - buf2 + 1);
				p[ptr2 - buf2] = 0;
				strings[stringcount] = p;
				stringcount++;
			}
			if (*c == '\'') {
				c++;
				int v = *c;
				c++;
				if (v == '\\') {
					c++;
					switch (*c) {
					case '0': v =  0; break;
					case 'n': v = 10; break;
					}
				}
				size_t l = sprintf(ptr, "%d", v);
				ptr += l;
				c++;
			}
			//if (*c == '\t')
			//	*c = ' ';
		}
		char *m = malloc(ptr - buf + 1);
		memcpy(m, buf, ptr - buf);
		m[ptr - buf] = 0;
		lines[linecount] = m;
		linecount++;
		c++;
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
	case '<':
	case '>':
		return 8;
	case '=':
	case '!':
		return 8;
	default:
		return 9999;
	}
}



static char *parseexpr(const char *p, struct func *f, size_t *k, int *etemp)
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

	union func_line_all_p fl;
	switch (i) {
	case 0:
		if (etemp)
			*etemp = 0;
		return strclone(words[0]);
	case 1:
		fprintf(stderr, "%d@%s: '%s' --> %d\n", __LINE__, __FILE__, p, i);
		abort();
	default: {
		if (etemp)
			*etemp = 1;
		char var[32];
		snprintf(var, sizeof var, "_var_%d", varcounter);
		varcounter++;
		char op;
		char swap = 0;
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
		case '>':
			swap = 1;
		case '<':
			op = MATH_LESS;
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

		char *x;
		char *ret;
		if (precdl >= precdr) {
			fl.d            = calloc(sizeof *fl.d, 1);
			fl.d->line.type = FUNC_LINE_DECLARE;
			fl.d->var       = strclone(var);
			f->lines[*k]    = fl.line;
			(*k)++;

			fl.m = calloc(sizeof *fl.m, 1);
			fl.m->line.type = FUNC_LINE_MATH;
			fl.m->op = op;
			fl.m->x = strclone(var);
			fl.m->y = strclone(words[0]);
			fl.m->z = strclone(words[2]);
			f->lines[*k] = fl.line;
			(*k)++;

			x = fl.m->x;
			if (words[1][0] == '=') { // meh
				fl.m = calloc(sizeof *fl.m, 1);
				fl.m->line.type = FUNC_LINE_MATH;
				fl.m->op = MATH_INV;
				fl.m->x  = x;
				fl.m->y  = x;
				f->lines[*k] = fl.line;
				(*k)++;
				//ret = fl.m->x;
			}

			// Dragons
			size_t ok = *k;
			char *v = fl.m->x;
			char *e = parseexpr(p, f, k, NULL);
			fl.line = f->lines[ok + 1]; // + 1 to skip DECLARE
			fl.m->y = v;

			ret = fl.m->x;
			ret = e;
			printf("%s\n", ret);
			if (!isnum(*v)) {
				fl.d            = calloc(sizeof *fl.d, 1);
				fl.d->line.type = FUNC_LINE_DESTROY;
				fl.d->var       = v;
				f->lines[*k]    = fl.line;
				(*k)++;
			}
		} else {
			char *e = parseexpr(p, f, k, NULL);

			fl.d            = calloc(sizeof *fl.d, 1);
			fl.d->line.type = FUNC_LINE_DECLARE;
			fl.d->var       = strclone(var);
			f->lines[*k]    = fl.line;
			(*k)++;

			fl.m = calloc(sizeof *fl.m, 1);
			fl.m->line.type = FUNC_LINE_MATH;
			fl.m->op = op;
			fl.m->x = strclone(var);
			fl.m->y = strclone(words[0]);
			fl.m->z = e;
			f->lines[*k] = fl.line;
			(*k)++;

			x   = fl.m->x;
			ret = fl.m->x;
			if (words[1][0] == '=') { // meh
				fl.m = calloc(sizeof *fl.m, 1);
				fl.m->line.type = FUNC_LINE_MATH;
				fl.m->op = MATH_INV;
				fl.m->x  = x;
				fl.m->y  = x;
				f->lines[*k] = fl.line;
				(*k)++;
				//ret = fl.m->x;
			}
		}
		return ret;
		//return fl.m->x;
	}
	}
}



static int parsefunc_header(struct func *f, const char *line)
{
	size_t i = 0, j = 0;

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
	if (line[j] != '(') {
		fprintf(stderr, "Expected '('\n");
		abort();
	}
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
}


static int parsefunc(size_t start)
{
	char *line = lines[start];
	struct func *f = &funcs[funccount];
	
	// Parse declaration
	parsefunc_header(f, line);

	// Parse lines
	start++;
	line = lines[start];
	size_t i = 0, j = 0, k = 0;
	struct func_line_goto  *loopjmps[16];
	struct func_line_math  *formath[16];
	char *loopvars[16];
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
		union func_line_all_p fl; // <-- Use this
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
						if (looptype[loopcount] == 1 &&
						    !isnum(*loopvars[loopcount])) {
							fl.d = calloc(sizeof *fl.d, 1);
							fl.d->line.type = FUNC_LINE_DESTROY;
							fl.d->var       = loopvars[loopcount];
							f->lines[k]     = fl.line;
							k++;
						}
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
					if (looptype[loopcount] == 1 &&
					    !isnum(*loopvars[loopcount])) {
						fl.d = calloc(sizeof *fl.d, 1);
						fl.d->line.type = FUNC_LINE_DESTROY;
						fl.d->var       = loopvars[loopcount];
						f->lines[k]     = fl.line;
						k++;
					}
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
			char *iterator = var;

			NEXTWORD;

			if (!streq(word, "in")) {
				fprintf(stderr, "Expected 'in', got '%s'\n", word);
				return -1;
			}

			char *p = ptr;
			char *fromarray = NULL;
			char *fromval, *toval;
			while (strncmp(ptr, " to ", 4) != 0) {
				ptr++;
				if (*ptr == 0) {
					fromarray = strclone(p);
					char b[64];
					snprintf(b, sizeof b, "%s.length", p);
					fromval   = "0";
					toval     = strclone(b);
					static size_t iteratorcount = 0;
					snprintf(b, sizeof b, "_for_iterator_%d", iteratorcount);
					iteratorcount++;
					iterator = strclone(b);
					goto isfromarray;
				}
			}
			memcpy(word, p, ptr - p);
			word[ptr - p] = 0;

			fromval = parseexpr(word, f, &k, NULL);

			NEXTWORD;
			NEXTWORD;

			toval = parseexpr(ptr, f, &k, NULL);

		isfromarray:
			// Set iterator
			flp.d            = calloc(sizeof *flp.d, 1);
			flp.d->line.type = FUNC_LINE_DECLARE;
			flp.d->var       = iterator;
			f->lines[k]      = flp.line;
			k++;

			struct func_line_assign *fla = calloc(sizeof *fla, 1);
			fla->line.type = FUNC_LINE_ASSIGN;
			fla->var       = iterator;
			fla->value     = fromval;
			f->lines[k]    = (struct func_line *)fla;
			k++;

			// Create the labels
			char lbl[16], lbll[16], lble[16];
		       	snprintf(lbl , sizeof lbl , ".for_%d"     , loopcounter);
		       	snprintf(lbll, sizeof lbll, ".for_%d_else", loopcounter);
		       	snprintf(lble, sizeof lble, ".for_%d_end" , loopcounter);

			// Indicate the start of the loop
			fl.l            = calloc(sizeof *fl.l, 1);
			fl.l->line.type = FUNC_LINE_LABEL;
			fl.l->label     = strclone(lbl);
			f->lines[k]     = fl.line;
			k++;

			// Test ending condition (fromval == toval)
			fl.i = calloc(sizeof *fl.i, 1);
			char expr[64];
			snprintf(expr, sizeof expr, "%s == %s", fla->var, toval);
			char *e = parseexpr(expr, f, &k, NULL);
			fl.i->line.type = FUNC_LINE_IF;
			fl.i->label     = strclone(lbll);
			fl.i->var       = e;
			f->lines[k]     = fl.line;
			k++;

			// Set variable to be used by the inner body of the loop
			// Only applies if fromarray is set
			if (fromarray != NULL) {
				fl.m          = calloc(sizeof *fl.m, 1);
				fl.line->type = FUNC_LINE_MATH;
				fl.m->op      = MATH_LOADAT;
				fl.m->x       = var;
				fl.m->y       = fromarray;
				fl.m->z       = iterator;
				f->lines[k]   = fl.line;
				k++;
			}

			// Destroy redundant testing variable
			fl.d = calloc(sizeof *fl.d, 1);
			fl.d->line.type = FUNC_LINE_DESTROY;
			fl.d->var       = e;
			f->lines[k]     = fl.line;
			k++;

			// Set loop incrementer
			formath[loopcount] = calloc(sizeof *formath[loopcount], 1);
			formath[loopcount]->line.type = FUNC_LINE_MATH;
			formath[loopcount]->op        = MATH_ADD;
			formath[loopcount]->x         = iterator;
			formath[loopcount]->y         = iterator;
			formath[loopcount]->z         = "1";

			// Set loop repeat goto
			loopjmps[loopcount] = calloc(sizeof *loopjmps[loopcount], 1);
			loopjmps[loopcount]->line.type = FUNC_LINE_GOTO;
			loopjmps[loopcount]->label     = strclone(lbl);

			// Set label for if the loop ended without interruption (break)
			loopelse[loopcount] = calloc(sizeof *loopelse[loopcount], 1);
			loopelse[loopcount]->line.type = FUNC_LINE_LABEL;
			loopelse[loopcount]->label     = strclone(lbll);

			// Set label for if the loop was interrupted
			loopends[loopcount] = calloc(sizeof *loopends[loopcount], 1);
			loopends[loopcount]->line.type = FUNC_LINE_LABEL;
			loopends[loopcount]->label     = strclone(lble);

			// Set the loop type
			looptype[loopcount] = 1;
			loopvars[loopcount] = toval;

			// Increment counters
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

			char *v = parseexpr(ptr, f, &k, NULL);
			char expr[64];
			snprintf(expr, sizeof expr, "%s == 0", v);
			char *e = parseexpr(expr, f, &k, NULL);
			flp.i = calloc(sizeof *flp.i, 1);
			flp.i->line.type = FUNC_LINE_IF;
			flp.i->label     = lbllc;
			flp.i->var       = e;
			f->lines[k]      = flp.line;
			k++;
			if (!isnum(*e)) {
				fl.d            = calloc(sizeof *fl.d, 1);
				fl.d->line.type = FUNC_LINE_DESTROY;
				fl.d->var       = e;
				f->lines[k]     = fl.line;
				k++;
			}

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

			//char *v = parseexpr(ptr, f, &k);
			char expr[64];
			snprintf(expr, sizeof expr, "%s == 0", ptr);

			char *e = parseexpr(expr, f, &k, NULL);
			printf("%s -- %s\n", expr, e);
			flp.i = calloc(sizeof *flp.i, 1);
			flp.i->line.type = FUNC_LINE_IF;
			flp.i->var   = e;
			flp.i->label = lbllc;
			f->lines[k]  = flp.line;
			k++;

			if (!isnum(*e)) {
				fl.d            = calloc(sizeof *fl.d, 1);
				fl.d->line.type = FUNC_LINE_DESTROY;
				fl.d->var       = e;
				f->lines[k]     = fl.line;
				k++;
			}

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
		} else if (streq(word, "long") || /* TODO */ streq(word, "char[]")) {
			NEXTWORD;
			char *name = strclone(word);
			fl.d            = calloc(sizeof *fl.d, 1);
			fl.d->line.type = FUNC_LINE_DECLARE;
			fl.d->var       = name;
			f->lines[k]     = fl.line;
			k++;
			NEXTWORD;
			if (streq(word, "=")) {
				fl.a = calloc(sizeof *fl.a, 1);
				fl.a->line.type = FUNC_LINE_ASSIGN;
				fl.a->var       = name;
				fl.a->value     = strclone(ptr);
				f->lines[k]     = fl.line;
				k++;
			}
		} else {
			char name[32];
			strcpy(name, word);
			NEXTWORD;

			if (streq(word, "=")) {
				char *p = ptr;
				NEXTWORD;
				printf("%s\n", word);
				if (h_get(&functbl, word) != -1) {
					fl.f          = calloc(sizeof *fl.f, 1);
					fl.line->type = FUNC_LINE_FUNC;
					fl.f->var     = strclone(name);
					strcpy(fl.f->name, word);
					NEXTWORD;
					if (word[0] != 0) {
						strcpy(fl.f->params[0], word);
						fl.f->paramcount++;
						while (1) {
							NEXTWORD;
							if (word[0] == 0)
								break;
							strcpy(fl.f->params[fl.f->paramcount], word);
							fl.f->paramcount++;
						}
					}
				} else {
					fl.a = calloc(sizeof *fl.a, 1);
					fl.a->line.type = FUNC_LINE_ASSIGN;
					fl.a->var       = strclone(name);
					fl.a->value     = strclone(p);
				}
				f->lines[k] = fl.line;
				k++;
			} else if (strchr("+-*/", word[0]) && word[1] == '=' &&
			           word[2] == 0) {
				int etemp;
				// Do math
				flp.m = calloc(sizeof *flp.m, 1);
				flp.m->line.type = FUNC_LINE_MATH;
				flp.m->op        = MATH_ADD; // TODO
				flp.m->x         = strclone(name);
				flp.m->y         = flp.m->x;
				flp.m->z         = parseexpr(ptr, f, &k, &etemp);
				char *v = flp.m->z;
				f->lines[k]      = flp.line;
				k++;
				// Destroy expression variable if newly allocated
				if (etemp) {
					fl.d          = calloc(sizeof *fl.d, 1);
					fl.line->type = FUNC_LINE_DESTROY;
					fl.d->var     = v;
					f->lines[k]   = fl.line;
					k++;
				}
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



static int optimizefunc_replace(struct func *f)
{
	struct hashtbl h;
	h_create(&h, 16);

	for (size_t i = 0; i < f->linecount; i++) {
		union func_line_all_p fl = { .line = f->lines[i] };
		size_t j;
		char  *v;
		size_t lastk;
		switch (fl.line->type) {
		case FUNC_LINE_ASSIGN:
			if (fl.a->cons && isnum(*fl.a->value)) {
				j = strtol(fl.a->value, NULL, 0);
				h_add(&h, fl.a->var, j);
			}
			v = fl.a->var;
			for (size_t k = i + 1; k < f->linecount; k++) {
				fl.line = f->lines[k];
				switch (fl.line->type) {
				case FUNC_LINE_ASSIGN:
					if (streq(v, fl.a->value))
						goto used;
					if (streq(v, fl.a->var))
						goto notused;
					break;
				case FUNC_LINE_IF:
					if (streq(v, fl.i->var))
						goto used;
					break;
				case FUNC_LINE_MATH:
					if (streq(v, fl.m->y) ||
					    (fl.m->z != NULL && streq(v, fl.m->z)))
						goto used;
					if (streq(v, fl.m->x))
						goto notused;
					break;
				}
			}
		notused:
			f->linecount--;
			memmove(f->lines + i, f->lines + i + 1,
				(f->linecount - i) * sizeof *f->lines);
		used:
			break;
		case FUNC_LINE_DECLARE:
			// Find the last occurence of the variable
			lastk = i;
			v     = fl.d->var;
			for (size_t k = lastk + 1; k < f->linecount; k++) {
				fl.line = f->lines[k];
				switch(fl.line->type) {
				case FUNC_LINE_ASSIGN:
					if (streq(v, fl.a->value) || streq(v, fl.a->var))
						lastk = k + 1;
					break;
				case FUNC_LINE_DECLARE:
					if (streq(v, fl.d->var)) {
						// This shouldn't happen before a destroy statement is encountered
						fprintf(stderr, "Double declare for variable '%s'\n", v);
						abort();
					}
				case FUNC_LINE_DESTROY:
					// Can the destroy statement be moved up?
					if (lastk + 1 != k) {
						f->linecount--;
						memmove(f->lines + k, f->lines + k + 1,
							(f->linecount - k) * sizeof *f->lines);
						goto move_destroy;
					} else {
						goto already_destroyed;
					}
				case FUNC_LINE_FUNC:
					if (fl.f->var != NULL && streq(v, fl.f->var)) {
						lastk = k + 1;
					} else {
						for (size_t p = 0; p < fl.f->paramcount; p++) {
							if (streq(v, fl.f->params[p])) {
								lastk = k + 1;
								break;
							}
						}
					}
					break;
				case FUNC_LINE_IF:
					if (streq(v, fl.i->var))
						lastk = k;
					break;
				case FUNC_LINE_MATH:
					if (streq(v, fl.m->x) || streq(v, fl.m->y) ||
					    (fl.m->z != NULL && streq(v, fl.m->z)))
						lastk = k + 1;
					break;
				}
			}
			// No destroy line found, so create one
			fl.line       = calloc(sizeof *fl.d, 1);
			fl.line->type = FUNC_LINE_DESTROY;
			fl.d->var     = v;
		move_destroy:
			memmove(f->lines + lastk + 1, f->lines + lastk,
				(f->linecount - lastk) * sizeof *f->lines);
			f->linecount++;
			f->lines[lastk] = fl.line;
		already_destroyed:
			break;
		case FUNC_LINE_IF:
			if (h_get2(&h, fl.i->var, &j) >= 0) {
				if ((j == 0) == fl.i->inv) {
					char *lbl   = fl.i->label;
					fl.g        = calloc(sizeof *fl.g, 1);
					fl.g->label = lbl;
					f->lines[i] = fl.line;
				} else {
					f->linecount--;
					memmove(f->lines + i, f->lines + i + 1,
					        (f->linecount - i) * sizeof *f->lines);
				}
			}
		case FUNC_LINE_MATH:
			if (0 && fl.m->z != NULL && isnum(*fl.m->z)) {
				if (fl.m->op == MATH_DIV) {
					size_t n = 0, l;
					size_t z = strtol(fl.m->z, NULL, 0);
					for (size_t k = 0; k < 64; k++) {
						if (z & 1) {
							n++;
							l = k;
						}
						z >>= 1;
					}
					if (n == 1) {
						fl.m->op = MATH_RSHIFT;
						char b[16];
						snprintf(b, sizeof b, "%lu", l);
						fl.m->z  = strclone(b);
					}
				} else if (fl.m->op == MATH_MOD) {
					size_t n = 0;
					size_t z = strtol(fl.m->z, NULL, 0), oz = z;
					for (size_t k = 0; k < 64; k++) {
						if (z & 1)
							n++;
						z >>= 1;
					}
					if (n == 1) {
						fl.m->op = MATH_AND;
						char b[16];
						snprintf(b, sizeof b, "0x%lx", oz - 1);
						fl.m->z  = strclone(b);
					}
				}
			}

			if (fl.m->op == MATH_ADD || fl.m->op == MATH_SUB) {
				if (streq(fl.m->y, "0") || streq(fl.m->z, "0")) {
					char *var = fl.m->x,
					     *val = streq(fl.m->y, "0") ? fl.m->z : fl.m->y;
					fl.a = calloc(sizeof *fl.a, 1);
					fl.a->line.type = FUNC_LINE_ASSIGN;
					fl.a->var = var;
					fl.a->value = val;
					f->lines[i] = fl.line;
				}
			} else if (fl.m->op == MATH_MUL) {
				if (!streq(fl.m->y, "0") && !streq(fl.m->z, "0"))
					break;
			} else if (fl.m->op == MATH_DIV || fl.m->op == MATH_MOD) {
				if (!streq(fl.m->z, "1"))
					break;
			} else {
				break;
			}
			break;
		}
	}

	return 0;
}


static int optimizefunc_peephole2(struct func *f)
{
	for (size_t i = 0; i < f->linecount - 1; i++) {
		union func_line_all_p fl0 = { .line = f->lines[i + 0] },
		                      fl1 = { .line = f->lines[i + 1] };
		switch (fl0.line->type) {
		case FUNC_LINE_MATH:
			if (fl1.line->type == FUNC_LINE_DESTROY) {
				char *v = fl1.d->var;
				if (!streq(v, fl0.m->x) && !streq(v, fl0.m->y) &&
				    (fl0.m->z == NULL || !streq(v, fl0.m->z))) {
					f->lines[i + 0] = fl1.line;
					f->lines[i + 1] = fl0.line;
				}
			}
			break;
		}
	}

	return 0;
}


static int optimizefunc_peephole3(struct func *f)
{
	for (size_t i = 0; i < f->linecount - 2; i++) {
		union func_line_all_p fl0 = { .line = f->lines[i + 0] },
		                      fl1 = { .line = f->lines[i + 1] },
				      fl2 = { .line = f->lines[i + 2] };
		switch (fl0.line->type) {
		case FUNC_LINE_DECLARE:
			if (fl1.line->type == FUNC_LINE_ASSIGN  &&
			    fl2.line->type == FUNC_LINE_DESTROY) {
				if (streq(fl0.d->var, fl1.a->var) &&
				    streq(fl1.a->value, fl2.d->var)) {
					char *v = fl0.d->var, *w = fl2.d->var;
					f->lines[i] = f->lines[i + 1];
					f->linecount -= 3;
					memmove(f->lines + i, f->lines + i + 3, (f->linecount - i) * sizeof f->lines[i]);
					i -= 3;
					for (size_t j = i; j < f->linecount; j++) {
						union func_line_all_p l = { .line = f->lines[j] };
						switch (l.line->type) {
						case FUNC_LINE_ASSIGN:
							if (streq(l.a->var, v))
								l.a->var = w;
							if (streq(l.a->value, v))
								l.a->value = w;
							break;
						case FUNC_LINE_DECLARE:
						case FUNC_LINE_DESTROY:
							if (streq(l.d->var, v))
								l.d->var = w;
							break;
						case FUNC_LINE_FUNC:
							for (size_t k = 0; k < l.f->paramcount; k++) {
								if (streq(l.f->params[k], v))
									strcpy(l.f->params[k], w);
							}
							break;
						case FUNC_LINE_IF:
							if (streq(l.i->var, v))
								l.i->var = w;
							break;
						case FUNC_LINE_MATH:
							if (streq(l.m->x, v))
								l.m->x = w;
							if (streq(l.m->y, v))
								l.m->x = w;
							if (l.m->z != NULL && streq(l.m->z, v))
								l.m->x = w;
							break;
						case FUNC_LINE_RETURN:
							if (streq(l.r->val, v))
								l.r->val = w;
							break;
						}
					}
				}
			}
			break;
		case FUNC_LINE_MATH:
			if (fl0.m->op == MATH_INV &&
			    fl1.line->type == FUNC_LINE_IF &&
			    fl2.line->type == FUNC_LINE_DESTROY) {
				if (streq(fl0.m->x, fl1.i->var) && streq(fl1.i->var, fl2.d->var)) {
					fl1.i->inv = !fl1.i->inv;
					f->linecount--;
					memmove(f->lines + i, f->lines + i + 1, (f->linecount - i) * sizeof f->lines[i]);
				}
			}
			break;
		}
	}

	return 0;
}



static int optimizefunc_peephole4(struct func *f)
{
	for (size_t i = 0; i < f->linecount - 3; i++) {
		union func_line_all_p fl0 = { .line = f->lines[i + 0] },
		                      fl1 = { .line = f->lines[i + 1] },
				      fl2 = { .line = f->lines[i + 2] },
				      fl3 = { .line = f->lines[i + 3] };
		switch (fl0.line->type) {
		case FUNC_LINE_DECLARE:
			if (fl1.line->type == FUNC_LINE_ASSIGN &&
			    fl2.line->type == FUNC_LINE_IF &&
			    fl3.line->type == FUNC_LINE_DESTROY &&
			    streq(fl0.d->var, fl3.d->var)) {
				if (streq(fl0.d->var, fl1.a->var) &&
				    streq(fl1.a->var, fl2.i->var)) {
					fl2.i->var = fl1.a->value;
					f->lines[i] = fl2.line;
					f->linecount -= 3;
					memmove(f->lines + i + 1, f->lines + i + 4, (f->linecount - i) * sizeof f->lines[i]);
				}
			}
			break;
		}
	}

	return 0;
}


static int optimizefunc_findconst(struct func *f)
{
	struct hashtbl h;
	h_create(&h, 16);
	for (size_t i = 0; i < f->linecount; i++) {
		union func_line_all_p l = { .line = f->lines[i] };
		size_t j;
		switch (l.line->type) {
		case FUNC_LINE_DECLARE:
			printf("D %s\n", l.d->var);
			h_add(&h, l.d->var, 0);
			break;
		case FUNC_LINE_ASSIGN:
			printf("A %s\n", l.a->var);
			j = h_get(&h, l.a->var);
			if (j == -1) {
				// Is a function. Skip
			} else if (j == 0) {
				// Not assigned yet, so potentially constant
				h_rem(&h, l.d->var);
				h_add(&h, l.d->var, i);
				l.a->cons = 1;
			} else {
				// Reassigned, so not constant
				((struct func_line_assign *)f->lines[j])->cons = 0;
				h_rem(&h, l.a->var);
			}
			break;
		case FUNC_LINE_MATH:
			j = h_get(&h, l.m->x);
			if (j != -1 && j != 0) {
				((struct func_line_assign *)f->lines[j])->cons = 0;
				h_rem(&h, l.m->x);
			}
			break;
		}
	}
	for (size_t i = 0; i < f->linecount; i++) {
		union func_line_all_p l = { .line = f->lines[i] };
		if (l.line->type == FUNC_LINE_DECLARE) {
			size_t j = h_get(&h, l.d->var);
			if (j != -1 && j != 0) {
				f->linecount--;
				memmove(f->lines + i, f->lines + i + 1, (f->linecount - i) * sizeof *f->lines);
			}
		}
	}
	return 0;
}



static int optimizefunc(struct func *f)
{
	// It works
	for (size_t i = 0; i < 5; i++) {
		if (optimizefunc_findconst(f) < 0 ||
		    optimizefunc_replace  (f) < 0 ||
		    optimizefunc_peephole2(f) < 0 ||
		    optimizefunc_peephole3(f) < 0 ||
		    optimizefunc_peephole4(f) < 0)
			return -1;
	}
	return 0;
}



static int lines2structs()
{
	h_create(&functbl, 4);
	// Find all functions
	for (size_t i = 0; i < linecount; i++) {
		char *line = lines[i];
		size_t l = strlen(line);
		struct func *f;
		// External function
		if (memcmp(line, "extern ", sizeof "extern " - 1) == 0) {
			f = calloc(sizeof *f, 1);
			parsefunc_header(f, line + sizeof "extern " - 1);
			h_add(&functbl, f->name, (size_t)f);
		// Internal function
		} else if (line[l - 1] == ')') {
			f = &funcs[funccount];
			parsefunc_header(f, line);
			h_add(&functbl, f->name, (size_t)f);
			funccount++;
			int nest = 1;
			// Find end of func
			do {
				i++;
				if (streq(lines[i], "end"))
					nest--;
				else if (memcmp(lines[i], "for"  , sizeof "for"   - 1) == 0 ||
				         memcmp(lines[i], "while", sizeof "while" - 1) == 0 ||
				         memcmp(lines[i], "if"   , sizeof "if"    - 1) == 0)
					nest++;
			} while (nest > 0);
		} else {
			fprintf(stderr, "Invalid function declaration: '%s'\n", line);
			abort();
		}
	}
	// Parse defined functions
	funccount = 0;
	for (size_t i = 0; i < linecount; i++) {
		char *line = lines[i];
		size_t l = strlen(line);
		// External function
		if (memcmp(line, "extern ", sizeof "extern " - 1) == 0) {
			continue;
		} else if (line[l - 1] == ')') {
			parsefunc(i);
#ifndef NO_OPT
			optimizefunc(&funcs[funccount]);
#endif
			funccount++;
		}
	}


	return 0;
}



static int func2vasm(struct func *f) {
	union vasm_all a;
	union func_line_all_p l;

	a.s.op  = VASM_OP_LABEL;
	a.s.str = f->name;
	vasms[vasmcount] = a.a;
	vasmcount++;

	struct hashtbl tbl;
	if (h_create(&tbl, 8) < 0) {
		perror("Failed to create hash table");
		return -1;
	}

	char allocated_regs[32];
	char is_const_reg_zero[32];
	memset(allocated_regs, 0, sizeof allocated_regs);
	memset(is_const_reg_zero, 0, sizeof is_const_reg_zero);

	// Add constants first (if not too many)
	size_t consts[8];
	size_t constcount = 0;
	for (size_t i = 0; i < f->linecount && constcount < sizeof consts / sizeof *consts; i++) {
		l.line = f->lines[i];
		switch (l.line->type) {
		case FUNC_LINE_ASSIGN:
			if (l.a->cons) {
				consts[constcount] = i;
				constcount++;
			}
			break;
		case FUNC_LINE_MATH:
			if (isnum(*l.m->y)) {
				consts[constcount] = i;
				constcount++;
			}
			if (l.m->z != NULL && isnum(*l.m->z)) {
				consts[constcount] = i;
				constcount++;
			}
			break;
		}
	}

	printf("%lu\n", constcount);
	if (constcount >= sizeof consts / sizeof *consts)
		constcount = 0;

	struct hashtbl constvalh;
	h_create(&constvalh, 16);
	for (size_t i = 0; i < constcount; i++) {
		l.line   = f->lines[consts[i]];
		a.op     = VASM_OP_SET;
		a.rs.r   = i;
		char *k, *v;
		char b[64];
		static size_t bc = 0;
		char use_y;
		switch (l.line->type) {
		case FUNC_LINE_ASSIGN:
			k = l.a->var;
			v = l.a->value;
			break;
		case FUNC_LINE_MATH:
			snprintf(b, sizeof b, "_const_%d", bc);
			bc++;
			k = strclone(b);
			if (isnum(*l.m->y)) {
				use_y = 1;
				v = l.m->y;
				l.m->y = k;
			} else {
				use_y = 0;
				v = l.m->z;
				l.m->z = k;
			}
			break;
		}
		printf("%s\n", v);
		char *ok = (char *)h_get(&constvalh, v);
		if (ok != (char *)-1) {
			printf("OLD %s\n", v);
			switch (l.line->type) {
			case FUNC_LINE_ASSIGN:
				break;
			case FUNC_LINE_MATH:
				if (use_y)
					l.m->y = ok;
				else
					l.m->z = ok;
				break;
			}
		} else {
			printf("NEW %s | %lx\n", v, k);
			a.rs.str = v;
			h_add(&tbl, k, i);
			h_add(&constvalh, v, (size_t)k);
			allocated_regs[i] = 1;
			is_const_reg_zero[i] =
				!(isnum(*v) && strtol(v, NULL, 0) == 0);
			vasms[vasmcount] = a.a;
			vasmcount++;
		}
	}

	for (size_t i = 0; i < f->linecount; i++) {
		union  func_line_all_p   fl = { .line = f->lines[i] };
		struct func_line_assign *fla;
		struct func_line_func   *flf;
		struct func_line_goto   *flg;
		struct func_line_if     *fli;
		struct func_line_label  *fll;
		struct func_line_math   *flm;
		struct func_line_return *flr;
		char ra, rb;
		size_t reg;
		switch (f->lines[i]->type) {
		case FUNC_LINE_ASSIGN:
			if (fl.a->cons && h_get(&tbl, fl.a->var) != -1)
				break;
			reg = h_get(&tbl, fl.a->var);
			if (reg == -1) {
				fprintf(stderr, "Variable '%s' not declared\n", fl.a->var);
				abort();
			}
			if ('0' <= *fl.a->var && *fl.a->var <= '9') {
				fprintf(stderr, "You can't assign to a number\n");
				abort();
			}
			if ('0' <= *fl.a->value && *fl.a->value <= '9') {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = reg;
				a.rs.str = fl.a->value;
			} else {
				a.r2.op   = VASM_OP_MOV;
				a.r2.r[0] = reg;
				reg = h_get(&tbl, fl.a->value);
				if (reg == -1) {
					fprintf(stderr, "Variable '%s' not declared\n", fl.a->value);
					abort();
				}
				a.r2.r[1] = reg;
			}
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_DECLARE:
			for (reg = 0; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
				if (!allocated_regs[reg]) {
					allocated_regs[reg] = 1;
					break;
				}
			}
			if (h_add(&tbl, fl.d->var, reg) < 0) {
				fprintf(stderr, "Failed to add variable to hashtable\n");
				abort();
			}
			break;
		case FUNC_LINE_DESTROY:
			// TODO
			if (fl.d->var[1] == '.')
				break;
			reg = h_get(&tbl, fl.d->var);
			if (reg == -1) {
				fprintf(stderr, "Variable '%s' not declared\n", fl.d->var);
				abort();
			}
			allocated_regs[reg] = 0;
			h_rem(&tbl, fl.d->var);
			break;
		case FUNC_LINE_FUNC:
			flf = (struct func_line_func *)f->lines[i];

			for (size_t j = flf->var ? 1 : 0; j < 32; j++) {
				if (allocated_regs[j]) {
					a.r.op = VASM_OP_PUSH;
					a.r.r  = j;
					vasms[vasmcount] = a.a;
					vasmcount++;
				}
			}
			for (size_t j = 0; j < flf->paramcount; j++) {
				size_t r = h_get(&tbl, flf->params[j]);
				if (r == -1) {
					a.rs.op  = VASM_OP_SET;
					a.rs.r   = j;
					a.rs.str = flf->params[j];
				} else {
					a.r2.op   = VASM_OP_MOV;
					a.r2.r[0] = j;
					a.r2.r[1] = r;
				}
				vasms[vasmcount] = a.a;
				vasmcount++;
			}

			// Call
			a.s.op  = VASM_OP_CALL;
			a.s.str = flf->name;
			vasms[vasmcount] = a.a;
			vasmcount++;

			// Pop registers
			for (int j = 31; j >= (flf->var ? 1 : 0); j--) {
				if (allocated_regs[j]) {
					a.r.op = VASM_OP_POP;
					a.r.r  = j;
					vasms[vasmcount] = a.a;
					vasmcount++;
				}
			}
			break;
		case FUNC_LINE_GOTO:
			flg = (struct func_line_goto *)f->lines[i];
			a.s.op  = VASM_OP_JMP;
			a.s.str = flg->label;
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_IF:
			fli = (struct func_line_if *)f->lines[i];
			if (isnum(*fli->var)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = fli->var;
				vasms[vasmcount] = a.a;
				vasmcount++;
			} else {
				ra = h_get(&tbl, fli->var);
				if (ra == -1) {
					fprintf(stderr, "Variable '%s' not declared\n", fl.i->var);
					abort();
				}
			}
			if (ra < constcount) {
				char jz = (fli->inv == VASM_OP_JZ);
				if (is_const_reg_zero[ra] != jz)
					break;
				a.s.op  = VASM_OP_JMP;
				a.s.str = fli->label;
			} else {
				rb = 0;
				a.r2s.op   = fli->inv ? VASM_OP_JZ : VASM_OP_JNZ;
				a.r2s.r[0] = ra;
				a.r2s.r[1] = rb;
				a.r2s.str  = fli->label;
			}
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_LABEL:
			fll = (struct func_line_label *)f->lines[i];
			a.s.op  = VASM_OP_LABEL;
			a.s.str = fll->label;
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_MATH:
			flm = (struct func_line_math *)f->lines[i];
			if (isnum(*flm->x)) {
				fprintf(stderr, "You can't assign to a number\n");
				abort();
			}
			if (isnum(*flm->y)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = flm->y;
				vasms[vasmcount] = a.a;
				vasmcount++;
			} else {
				ra = h_get(&tbl, flm->y);
				if (ra == -1) {
					printf("aaa\n");
					fprintf(stderr, "Variable '%s' not declared\n", flm->y);
					abort();
				}
			}
			if (flm->op != MATH_INV && flm->op != MATH_NOT) {
				if (isnum(*flm->z)) {
					a.rs.op  = VASM_OP_SET;
					a.rs.r   = rb = 21;
					a.rs.str = flm->z;
					vasms[vasmcount] = a.a;
					vasmcount++;
				} else {
					// TODO
					if (flm->z[1] == '.') {
						char l[64], r[64];
						memcpy(l, flm->z, 1);
						l[1] = 0;
						memcpy(r, flm->z + 2, strlen(flm->z) - 2);
						r[strlen(flm->z) - 2] = 0;
						printf("%s\n", r);

						a.rs.op  = VASM_OP_SET;
						a.rs.r   = 22;
						a.rs.str = "-1";
						vasms[vasmcount] = a.a;
						vasmcount++;

						rb = 21;
						a.r3.op   = VASM_OP_LOADLAT;
						a.r3.r[0] = rb;
						a.r3.r[1] = h_get(&tbl, l);
						a.r3.r[2] = 22;
						vasms[vasmcount] = a.a;
						vasmcount++;
					} else {
						// TODO
						rb = h_get(&tbl, flm->z);
						if (rb == -1) {
							fprintf(stderr, "Variable '%s' not declared\n", flm->z);
							abort();
						}
					}
				}
				a.r3.op   = flm->op;
				a.r3.r[0] = h_get(&tbl, flm->x);
				a.r3.r[1] = ra;
				a.r3.r[2] = rb;
				if (a.r3.r[0] == -1) {
					size_t reg = 0;
					for ( ; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
						if (!allocated_regs[reg]) {
							allocated_regs[reg] = 1;
							break;
						}
					}
					a.r3.r[0] = reg;
					h_add(&tbl, flm->x, reg);
				}
			} else {
				a.r2.op   = flm->op;
				a.r2.r[0] = h_get(&tbl, flm->x);
				a.r2.r[1] = ra;
				if (a.r2.r[0] == -1) {
					size_t reg = 0;
					for ( ; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
						if (!allocated_regs[reg]) {
							allocated_regs[reg] = 1;
							break;
						}
					}
					a.r2.r[0] = reg;
					h_add(&tbl, flm->x, reg);
				}
			}
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_RETURN:
			flr = (struct func_line_return *)f->lines[i];
			a.rs.op  = VASM_OP_SET;
			a.rs.r   = 0;
			a.rs.str = flr->val;
			vasms[vasmcount] = a.a;
			vasmcount++;
			vasms[vasmcount].op = VASM_OP_RET;
			vasmcount++;
			break;
		default:
			fprintf(stderr, "Unknown line type (%d)\n", f->lines[i]->type);
			abort();
		}
	}
	a.a.op  = VASM_OP_RET;
	vasms[vasmcount] = a.a;
	vasmcount++;
	return 0;
}




static int optimizevasm_replace(void)
{
	for (size_t i = 0; i < vasmcount; i++) {
		union vasm_all a = { .a = vasms[i] };
		size_t reg;
		switch (a.op) {
		case VASM_OP_SET:
			break; // TODO
			reg = a.r.r;
			for (size_t j = i + 1; j < vasmcount; j++) {
				a.a = vasms[j];
			}
			break;
		case VASM_OP_POP:
			reg = a.r.r;
			a.a = vasms[i + 1];
			if (a.op == VASM_OP_PUSH && a.r.r == reg) {
				for (size_t j = i + 2; j < vasmcount; j++) {
					a.a = vasms[j];
					switch (a.op) {
					case VASM_OP_SET:
						if (a.rs.r == reg)
							goto unused;
						break;
					case VASM_OP_POP:
						if (a.r.r == reg)
							goto unused;
						break;
					case VASM_OP_PUSH:
					case VASM_OP_JZ:
					case VASM_OP_JNZ:
					//case VASM_OP_JG:
					//case VASM_OP_JGE:
						if (a.r.r == reg)
							goto used;
						break;
					case VASM_OP_ADD:
					case VASM_OP_SUB:
					case VASM_OP_MUL:
					case VASM_OP_DIV:
					case VASM_OP_MOD:
					case VASM_OP_REM:
					case VASM_OP_RSHIFT:
					case VASM_OP_LSHIFT:
					case VASM_OP_AND:
					case VASM_OP_OR:
					case VASM_OP_XOR:
						if (a.r3.r[1] == reg || a.r3.r[2] == reg)
							goto used;
						if (a.r3.r[0] == reg)
							goto unused;
						break;
					case VASM_OP_MOV:
					case VASM_OP_NOT:
					case VASM_OP_INV:
						if (a.r3.r[1] == reg)
							goto used;
						if (a.r3.r[0] == reg)
							goto unused;
						break;
					}
				}
			unused:
				vasmcount -= 2;
				memmove(vasms + i, vasms + i + 2, (vasmcount - i) * sizeof *vasms);
				i -= 4;
			used:
				;
			}
			break;
		case VASM_OP_MOV:
			if (a.r2.r[0] == a.r2.r[1]) {
				vasmcount--;
				memmove(vasms + i, vasms + i + 1, (vasmcount - i) * sizeof *vasms);
				i--;
			}
			break;
		}
	}

	return 0;
}


static int optimizevasm_peephole2(void)
{
	for (size_t i = 0; i < vasmcount - 1; i++) {
		union vasm_all a0 = { .a = vasms[i + 0] },
		               a1 = { .a = vasms[i + 1] };
		switch (a0.op) {
		case VASM_OP_RET:
			if (a1.op == VASM_OP_RET) {
				vasmcount--;
				memmove(vasms + i + 1, vasms + i + 3, (vasmcount - i) * sizeof vasms[i]);
				i -= 2;
			}
			break;
		}
	}

	return 0;
}


static int optimizevasm_peephole3(void)
{
	for (size_t i = 0; i < vasmcount - 2; i++) {
		union vasm_all a0 = { .a = vasms[i + 0] },
		               a1 = { .a = vasms[i + 1] },
		               a2 = { .a = vasms[i + 2] };
		switch (a0.op) {
		case VASM_OP_JZ:
		case VASM_OP_JNZ:
			if (a1.op == VASM_OP_JMP   &&
			    a2.op == VASM_OP_LABEL &&
			    streq(a0.rs.str, a2.s.str)) {
				a0.op = (a0.op == VASM_OP_JZ) ? VASM_OP_JNZ : VASM_OP_JZ;
				a0.rs.str = a1.s.str;
				vasms[i] = a0.a;
				vasmcount--;
				memmove(vasms + i + 1, vasms + i + 2, (vasmcount - i) * sizeof vasms[i]);
				i -= 3;
			}
			break;
		}
	}

	return 0;
}


static int optimizevasm_peephole4(void)
{
	for (size_t i = 0; i < vasmcount - 3; i++) {
		union vasm_all a0 = { .a = vasms[i + 0] },
		               a1 = { .a = vasms[i + 1] },
		               a2 = { .a = vasms[i + 2] },
		               a3 = { .a = vasms[i + 3] };
		switch (a0.op) {
		case VASM_OP_PUSH:
			if (a1.op == VASM_OP_CALL &&
			    a2.op == VASM_OP_POP  &&
			    a0.r.r == a2.r.r) {
				switch (a3.op) {
				case VASM_OP_SET:
					if (a3.rs.r == a0.r.r)
						goto remove_push;
					goto dont_remove_push;
				}
			}
			goto dont_remove_push;
		remove_push:
			vasmcount -= 2;
			vasms[i] = vasms[i + 1];
			memmove(vasms + i + 1, vasms + i + 3, (vasmcount - i) * sizeof vasms[i]);
		dont_remove_push:
			break;
		}
	}

	return 0;
}



static int structs2vasm() {
	for (size_t i = 0; i < funccount; i++)
		func2vasm(&funcs[i]);
#ifndef NO_OPT
	for (size_t i = 0; i < 5; i++) {
		optimizevasm_replace();
		optimizevasm_peephole2();
		optimizevasm_peephole3();
		optimizevasm_peephole4();
	}
#endif
	return 0;
}


int main(int argc, char **argv) {
	
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <input> <output>", argv[0]);
		return 1;
	}

	// Read source
	char buf[0x10000];
	int fd = open(argv[1], O_RDONLY);
	read(fd, buf, sizeof buf);
	close(fd);

	// 0. Text to lines
	printf("=== text2lines ===\n");
	text2lines(buf);
	printf("\n");
	for (size_t i = 0; i < stringcount; i++)
		printf(".str_%i = \"%s\"\n", i, strings[i]);
	printf("\n");
	for (size_t i = 0; i < linecount; i++)
		printf("%4d | %s\n", i + 1, lines[i]);
	printf("\n");

	// 1. Lines to structs
	printf("=== lines2structs ===\n");
	lines2structs();
	printf("\n");
	for (size_t i = 0; i < funccount; i++) {
		struct func *f = &funcs[i];
		printf("Name:   %s\n", f->name);
		printf("Return: %s\n", f->type);
		printf("Args:   %d\n", f->argcount);
		for (size_t j = 0; j < f->argcount; j++)
			printf("  %s -> %s\n", f->args[j].name, f->args[j].type);
		printf("Lines:  %d\n", f->linecount);
		for (size_t j = 0; j < f->linecount; j++) {
			union  func_line_all_p   fl = { .line = f->lines[j] } ;
			struct func_line_assign *fla;
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
	printf("\n");

	// 3. Structs to virt assembly
	fd = open(argv[2], O_WRONLY | O_CREAT);
	write(fd, vbin, vbinlen);
	close(fd);
	printf("=== structs2vasm ===\n");
	structs2vasm();
	printf("\n");
	FILE *_f = fopen(argv[2], "w");
	#define teeprintf(...) do {             \
		fprintf(stderr, ##__VA_ARGS__); \
		fprintf(_f, ##__VA_ARGS__);     \
	} while (0)
	for (size_t i = 0; i < vasmcount; i++) {
		union vasm_all a;
		a.a = vasms[i];
		switch (a.a.op) {
		default:
			fprintf(stderr, "Unknown OP (%d)\n", vasms[i].op);
			abort();
		case VASM_OP_NONE:
			teeprintf("\n");
			break;
		case VASM_OP_COMMENT:
			for (size_t j = 0; j < vasmcount; j++) {
				if (vasms[j].op == VASM_OP_NONE || vasms[j].op == VASM_OP_COMMENT)
					continue;
				if (vasms[j].op == VASM_OP_LABEL)
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
		teeprintf(".str_%d:\n"
		          "\t.long %lu\n"
		          "\t.str \"%s\"\n",
			  i, strlen(strings[i]), strings[i]);
	}
	printf("\n");

	/*
	// Write binary shit
	fd = open(argv[2], O_WRONLY | O_CREAT);
	write(fd, vbin, vbinlen);
	close(fd);
	*/

	// Yay
	return 0;
}

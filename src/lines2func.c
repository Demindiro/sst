#include "lines2func.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "func.h"
#include "hashtbl.h"
#include "util.h"


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
		ERROR("%d@%s: '%s' --> %lu", __LINE__, __FILE__, p, i);
		abort();
	default: {
		if (etemp)
			*etemp = 1;
		char var[32];
		snprintf(var, sizeof var, "_var_%d", varcounter);
		varcounter++;
		char op;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
		char swap = 0; // "Unused" my ass
#pragma GCC diagnostic pop
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



int parsefunc_header(struct func *f, const line_t line, const char *text)
{
	size_t i = 0, j = 0;
	const char *t = line.text;

	// Skip "extern"
	if (strstart(t, "extern "))
		i += strlen("extern ");

	j = i;

	// Get type
	while (t[j] != ' ') {
		j++;
		if (t[j] == 0) {
			ERROR("Expected function name");
			PRINTLINEX(line, j, text);
			EXIT(1);
		}
	}
	memcpy(f->type, t + i, j - i);

	// Skip whitespace
	while (t[j] == ' ')
		j++;
	i = j;

	// Get name
	while (t[j] != ' ' && t[j] != '(') {
		j++;
		if (t[j] == 0) {
			ERROR("Expected '('");
			PRINTLINEX(line, j, text);
			EXIT(1);
		}
	}
	memcpy(f->name, t + i, j - i);

	// Skip whitespace
	while (t[j] == ' ')
		j++;
	i = j;

	// Parse arguments
	if (t[j] != '(') {
		ERROR("Expected '('");
		PRINTLINEX(line, j, text);	
		EXIT(1);
	}
	j++;
	size_t k = 0;
	for ( ; ; ) {
		// Argument list done
		if (t[j] == ')')
			break;
		// Skip whitespace
		while (t[j] == ' ')
			j++;
		i = j;
		// Get type
		while (t[j] != ' ')
			j++;
		memcpy(f->args[k].type, t + i, j - i);
		// Skip whitespace
		while (t[j] == ' ')
			j++;
		i = j;
		// Get type
		while (t[j] != ' ' && t[j] != ')')
			j++;
		memcpy(f->args[k].name, t + i, j - i);
		// Skip whitespace
		while (t[j] == ' ')
			j++;
	}
	f->argcount = k;
	return 0;
}


int lines2func(const line_t *lines, size_t linecount,
               struct func *f, struct hashtbl *functbl)
{
	size_t li = 0;
	line_t line = lines[li];
	size_t k = 0;
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
			const char *_ptr = ptr; \
			while (*ptr != ' ' && *ptr != 0) \
				ptr++; \
			memcpy(word, _ptr, ptr - _ptr); \
			word[ptr - _ptr] = 0; \
			if (*ptr != 0) \
				ptr++; \
		} while (0)

		// Parse first word
		char word[32];
		const char *ptr = line.text;
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
						ERROR("Invalid loop type (%d)", looptype[loopcount]);
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
					ERROR("Invalid loop type (%d)", looptype[loopcount]);
					abort();
				}
				loopcount++;
			} else {
				ERROR("Unexpected 'else'");
				abort();
			}

		} else if (streq(word, "for")) {

			NEXTWORD;

			char *var = strclone(word);
			char *iterator = var;

			NEXTWORD;

			if (!streq(word, "in")) {
				ERROR("Expected 'in', got '%s'", word);
				return -1;
			}

			const char *p = ptr;
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
					snprintf(b, sizeof b, "_for_iterator_%lu", iteratorcount);
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
				const char *p = ptr;
				NEXTWORD;
				if (h_get(functbl, word) != -1) {
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
		li++;
		line = lines[li];
	}
	f->linecount = k;

	return 0;
}

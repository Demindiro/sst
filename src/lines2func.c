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


static const char *deref_arr(const char *w, struct func *f, size_t *k,
                             struct hashtbl *vartypes, int *etemp)
{
	static int counter = 0;
	const char *p = strchr(w, '[');
	if (p != NULL) {
		p++;
		char *q = strchr(p, ']');
		char var[16];
		snprintf(var, sizeof var, "_elem_%d", counter++);
		char *v = strclone(var);
		struct func_line_declare *d = malloc(sizeof *d);
		d->line.type = FUNC_LINE_DECLARE;
		d->var = v;
		f->lines[*k] = (struct func_line *)d;
		(*k)++;
		struct func_line_math *m = malloc(sizeof *m);
		m->line.type = FUNC_LINE_MATH;
		m->op = MATH_LOADAT;
		m->x  = v;
		m->y  = strnclone(w, p - w - 1);
		m->z  = strnclone(p, q - p);
		f->lines[*k] = (struct func_line *)m;
		(*k)++;
		if (etemp)
			*etemp = 1;
		return v;
	}
	if (etemp)
		*etemp = 0;
	return w;
}


/**
 * Dereference 'pointers' of the form 'container.member'
 * e.g.
 *     char[] a = new char[16]
 *     long b = a.length
 * expands to
 *     DECLARE char[] a
 *     ASSIGN  a = new char[16] # ?
 *     DECLARE long* _ptr
 *     MATH    _ptr = a - 1
 *     DECLARE long _temp
 *     MATH    _temp = _ptr[0]
 * (The length is stored as a long right in front of the array)
 *
 * There are also special cases. e.g.
 *     char[4096] a
 *     long  l = a.length;
 *     char* p = a.ptr;
 * expands to
 *     DECLARE char[4096] a;
 *     DECLARE long l
 *     ASSIGN  l = 4096
 *     DECLARE char* p
 *     ASSIGN  p = a
 * (l is set to 4096 directly because the array size is constant anyways)
 */
static const char *deref_var(const char *m, func f, size_t *k,
                             hashtbl vartypes, int *etemp)
{
	const char *c = m;
	const char *var;
	const char *array/*, *index*/;
	const char *parent, *member;
	int deref_type = -1;

	// Check dereference type
	while (*c != 0) {
		if (*c == '[') {
			deref_type = 0;
			var = array = strnclone(m, c - m);
			c++;
			const char *d = c;
			c = strchr(d, ']');
			if (c == NULL) {
				ERROR("Expected ']'");
				EXIT(1);
			}
			c--;
			//index = strnclone(d, c - d);
			break;
		} else if (*c == '.') {

			deref_type = 1;
			var = parent = strnclone(m, c - m);
			c++;
			member = strclone(c);
			break;
		}
		c++;
	}

	// Get type
	const char *type;
	if (deref_type != -1) {
		if (h_get2(vartypes, var, (size_t *)&type) < 0) {
			DEBUG("%s", m);
			ERROR("Variable '%s' is not declared", var);
			EXIT(1);
		}
	}

	switch (deref_type) {
	// No dereference
	case -1:
		if (etemp)
			*etemp = 0;
		return m;
	// Array access
	case 0:
		return deref_arr(m, f, k, vartypes, etemp);
	// Member access
	case 1: {
		size_t l = strlen(type);
		if (type[l - 1] == ']') {
			if (type[l - 2] == '[') {
				if (streq(member, "length")) {
					// Dynamic array
					const char *pointer = _new_temp_var(f, k, "long*", "pointer");
					line_math(f, k, MATH_SUB, pointer, parent, "8");
					const char *length  = _new_temp_var(f, k, "long", "length");
					line_math(f, k, MATH_LOADAT, length, pointer, "0");
					line_destroy(f, k, pointer);
					if (etemp) *etemp = 1;
					return length;
				} else if (streq(member, "ptr")) {
					if (etemp) *etemp = 0;
					return parent;
				} else {
					ERROR("Dynamic array '%s' doesn't have member '%s'", parent, member);
					EXIT(1);
				}
			} else {
				// Fixed array
				if (streq(member, "length")) {
					size_t i = l - 3;
					while (type[i] != '[')
						i--;
					i++;
					if (etemp) *etemp = 0;
					return strnclone(type + i, l - 1 - i);
				} else if (streq(member, "ptr")) {
					if (etemp) *etemp = 0;
					return parent;
				} else {
					ERROR("Fixed array '%s' doesn't have member '%s'", parent, member);
					EXIT(1);
				}
			}
		} else {
			ERROR("'%s' doesn't have member '%s'", parent, member);
			EXIT(1);
		}
	}
	}
	EXIT(2);
}


static const char *parseexpr(const char *p, struct func *f, size_t *k, int *etemp,
                             const char *temptype, struct hashtbl *vartypes)
{
	const char *orgptr = p;
	// TODO
	if (strstart(p, "new ")) {
		// Forgive me for I am lazy
		const char *v = _new_temp_var(f, k, strclone(p + 4), "new");
		const char *l = strnclone(strchr(p, '[') + 1, 1); // *puke*
		const char *a[1] = { l };
		line_function(f, k, v, "alloc", 1, a);
		if (etemp) *etemp = 1;
		return v;
	}
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
		return deref_var(strclone(words[0]), f, k, vartypes, etemp);
	case 1:
		ERROR("This situation is impossible");
		ERROR("Expression: '%s'", orgptr);
		EXIT(1);
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

		const char *x;
		const char *ret;
		if (precdl >= precdr) {
			fl.d            = calloc(sizeof *fl.d, 1);
			fl.d->line.type = FUNC_LINE_DECLARE;
			fl.d->type      = temptype;
			fl.d->var       = strclone(var);
			h_add(vartypes, fl.d->var, (size_t)fl.d->type);
			f->lines[*k]    = fl.line;
			(*k)++;

			fl.m = calloc(sizeof *fl.m, 1);
			fl.m->line.type = FUNC_LINE_MATH;
			fl.m->op = op;
			fl.m->x = strclone(var);
			fl.m->y = strclone(words[0]);
			fl.m->z = strclone(words[2]);
			if (swap)
				SWAP(const char *, fl.m->y, fl.m->z);
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
			const char *v = fl.m->x;
			const char *e = parseexpr(p, f, k, NULL, temptype, vartypes);
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
			const char *e = parseexpr(p, f, k, NULL, temptype, vartypes);

			fl.d            = calloc(sizeof *fl.d, 1);
			fl.d->line.type = FUNC_LINE_DECLARE;
			fl.d->type      = temptype;
			fl.d->var       = strclone(var);
			h_add(vartypes, fl.d->var, (size_t)fl.d->type);
			f->lines[*k]    = fl.line;
			(*k)++;

			fl.m = calloc(sizeof *fl.m, 1);
			fl.m->line.type = FUNC_LINE_MATH;
			fl.m->op = op;
			fl.m->x = strclone(var);
			int ee;
			fl.m->y = deref_var(strclone(words[0]), f, k, vartypes, &ee);
			fl.m->z = e;
			f->lines[*k] = fl.line;
			(*k)++;

			if (swap)
				SWAP(const char *, fl.m->y, fl.m->z);

			if (ee)
				line_destroy(f, k, fl.m->y);

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


static int is_array_dereference(const char *str, const char **arr, const char **index)
{
	const char *p = strchr(str, '[');
	if (p != NULL) {
		p++;
		const char *q = strchr(p, ']');
		if (q == NULL) {
			ERROR("Expected ']'");
			EXIT(1);
		}
		*arr   = strnclone(str, p - str - 1);
		*index = strnclone(p, q - p);
		return 1;
	}
	return 0;
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
	f->type = strnclone(t + i, j - i);

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
	f->name = strnclone(t + i, j - i);

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
		f->args[k].type = strnclone(t + i, j - i);
		// Skip whitespace
		while (t[j] == ' ')
			j++;
		i = j;
		// Get type
		while (t[j] != ' ' && t[j] != ')')
			j++;
		f->args[k].name = strnclone(t + i, j - i);
		// Skip whitespace
		while (t[j] == ' ')
			j++;
		k++;
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
	const char *loopvars[16];
	struct func_line_label *loopelse[16];
	struct func_line_label *loopends[16];
	char looptype[16];
	int  loopcounter = 0;
	int  loopcount   = 0;
	struct hashtbl vartypes;
	h_create(&vartypes, 16);

	// Add function parameters to vartypes
	for (size_t i = 0; i < f->argcount; i++)
		h_add(&vartypes, f->args[i].name, (size_t)f->args[i].type);

	for ( ; ; ) {

		#define NEXTWORD do {				\
			oldptr = ptr;				\
			const char *_ptr = ptr;			\
			while (*ptr != ' ' && *ptr != 0)	\
				ptr++;				\
			memcpy(word, _ptr, ptr - _ptr);		\
			word[ptr - _ptr] = 0;			\
			if (*ptr != 0)				\
				ptr++;				\
		} while (0)

		// Parse first word
		char word[32];
		const char *ptr = line.text, *oldptr;
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

			const char *var = strclone(word);
			const char *iterator = var;

			NEXTWORD;

			if (!streq(word, "in")) {
				ERROR("Expected 'in', got '%s'", word);
				return -1;
			}

			const char *p = ptr;
			const char *fromarray = NULL;
			const char *fromval, *toval;
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

			fromval = parseexpr(word, f, &k, NULL, "long", &vartypes);

			NEXTWORD;
			NEXTWORD;

			toval = parseexpr(ptr, f, &k, NULL, "long", &vartypes);

		isfromarray:
			// Set iterator
			flp.d            = calloc(sizeof *flp.d, 1);
			flp.d->line.type = FUNC_LINE_DECLARE;
			flp.d->type      = "long";
			flp.d->var       = iterator;
			h_add(&vartypes, flp.d->var, (size_t)flp.d->type);
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
			const char *e = parseexpr(expr, f, &k, NULL, "long", &vartypes);
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

			const char *v = parseexpr(ptr, f, &k, NULL, "bool", &vartypes);
			char expr[64];
			snprintf(expr, sizeof expr, "%s == 0", v);
			const char *e = parseexpr(expr, f, &k, NULL, "bool", &vartypes);
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
			const char *lbllc = strclone(lbll);
			const char *lblec = strclone(lble);

			char expr[64];
			snprintf(expr, sizeof expr, "%s == 0", ptr);

			const char *e = parseexpr(expr, f, &k, NULL, "bool", &vartypes);
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
		} else if (
			streq(word, "bool") || streq(word, "byte") || streq(word, "byte[21]") ||
			streq(word, "byte[4096]") ||
			streq(word, "long") || /* TODO */ streq(word, "byte[]")) {
			char *type = strclone(word);
			NEXTWORD;
			char *name = strclone(word);
			fl.d            = calloc(sizeof *fl.d, 1);
			fl.d->line.type = FUNC_LINE_DECLARE;
			fl.d->type      = type;
			fl.d->var       = name;
			h_add(&vartypes, fl.d->var, (size_t)fl.d->type);
			f->lines[k]     = fl.line;
			k++;
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
					fl.f->name    = strclone(word);
					const char *oldptr = ptr;
					NEXTWORD;
					int         etemp[64];
					const char *evars[64];
					if (word[0] != 0) {
						char b[256];
						ptr = oldptr;
						while (*ptr != ',' && *ptr != 0)
							ptr++;
						fl.f->args = malloc(16 * sizeof *fl.f->args);
						size_t l = ptr - oldptr;
						ptr++;
						memcpy(b, oldptr, l);
						b[l] = 0;
						fl.f->args[0] = parseexpr(b, f, &k, &etemp[0], "TODO_0", &vartypes);
						evars[0] = fl.f->args[0];
						fl.f->argcount = 1;
						while (*ptr != 0) {
							oldptr = ptr;
							while (*ptr != ',' && *ptr != 0)
								ptr++;
							size_t l = ptr - oldptr;
							ptr++;
							memcpy(b, oldptr, l);
							b[l] = 0;
							fl.f->args[fl.f->argcount] =
								parseexpr(b, f, &k,
								&etemp[fl.f->argcount],
								"TODO_1", &vartypes);
							evars[fl.f->argcount] = fl.f->args[fl.f->argcount];
							fl.f->argcount++;
						}
					}
					f->lines[k++]   = fl.line;
					for (size_t i = 0; i < fl.f->argcount; i++) {
						if (etemp[i]) {
							fl.d = malloc(sizeof *fl.d);
							fl.d->line.type = FUNC_LINE_DESTROY;
							fl.d->var = evars[i];
							f->lines[k++] = fl.line;
						}
					}
				} else {
					int etemp;
					const char *type;
					const char *arr, *index;
					const char *v;
					if (is_array_dereference(name, &arr, &index)) {
						if (h_get2(&vartypes, arr, (size_t *)&type) < 0) {
							ERROR("Variable '%s' not declared", arr);
							EXIT(1);
						}
						const char *de    = strchr(type, '[');
						const char *dtype = strnclone(type, de - type);
						fl.s = malloc(sizeof *fl.s);
						fl.s->line.type = FUNC_LINE_STORE;
						fl.s->var   = arr;
						fl.s->index = index;
						fl.s->val   = parseexpr(p, f, &k, &etemp,
								dtype, &vartypes);
						v = fl.s->val;
					} else {
						if (h_get2(&vartypes, name, (size_t *)&type) < 0) {
							ERROR("Variable '%s' not declared", name);
							EXIT(1);
						}
						fl.a = malloc(sizeof *fl.a);
						fl.a->line.type = FUNC_LINE_ASSIGN;
						fl.a->var       = strclone(name);
						fl.a->value     = parseexpr(p,
								f, &k, &etemp, type, &vartypes);
						v = fl.a->value;
					}
					f->lines[k++]   = fl.line;
					// Destroy expression variable if newly allocated
					if (etemp) {
						fl.d          = calloc(sizeof *fl.d, 1);
						fl.line->type = FUNC_LINE_DESTROY;
						fl.d->var     = v;
						f->lines[k]   = fl.line;
						k++;
					}
				}
			} else if (strchr("+-*/%<>", word[0]) && word[1] == '=' &&
			           word[2] == 0) {
				int etemp;
				const char *type;
				if (h_get2(&vartypes, name, (size_t *)&type) < 0) {
					ERROR("Variable '%s' not declared", name);
					EXIT(1);
				}
				// Do math
				flp.m = calloc(sizeof *flp.m, 1);
				flp.m->line.type = FUNC_LINE_MATH;
				flp.m->op        = MATH_ADD; // TODO
				flp.m->x         = strclone(name);
				flp.m->y         = flp.m->x;
				flp.m->z         = parseexpr(ptr, f, &k, &etemp, type, &vartypes);
				const char *v    = flp.m->z;
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
				flf->name = strclone(name);
				int         etemp[64];
				const char *evars[64];
				memset(etemp, 0, sizeof etemp);
				if (word[0] != 0) {
					char b[256];
					ptr = oldptr;
					while (*ptr != ',' && *ptr != 0)
						ptr++;
					flf->args = malloc(16 * sizeof *fl.f->args);
					size_t l = ptr - oldptr;
					ptr++;
					memcpy(b, oldptr, l);
					b[l] = 0;
					flf->args[0] = parseexpr(b, f, &k, &etemp[0], "TODO_2", &vartypes);
					evars[0] = flf->args[0];
					flf->argcount = 1;
					while (*ptr != 0) {
						oldptr = ptr;
						while (*ptr != ',' && *ptr != 0)
							ptr++;
						size_t l = ptr - oldptr;
						ptr++;
						memcpy(b, oldptr, l);
						b[l] = 0;
						flf->args[flf->argcount] =
							parseexpr(b, f, &k,
							&etemp[flf->argcount],
							"TODO_3", &vartypes);
						evars[flf->argcount] = flf->args[flf->argcount];
						flf->argcount++;
					}
				}
				flf->line.type = FUNC_LINE_FUNC;
				f->lines[k++] = (struct func_line *)flf;
				for (size_t i = 0; i < flf->argcount; i++) {
					if (etemp[i]) {
						fl.d = malloc(sizeof *fl.d);
						fl.d->line.type = FUNC_LINE_DESTROY;
						fl.d->var = evars[i];
						f->lines[k++] = fl.line;
					}
				}
			}
		}

		// NEXT PLEASE!!!
		li++;
		line = lines[li];
	}
	f->linecount = k;

	return 0;
}

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


static const char *deref_arr(const char *w, struct func *f,
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
		line_declare(f, v, NULL);
		line_math(f, MATH_LOADAT, v, strnclone(w, p - w - 1),
		          strnclone(p, q - p));
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
static const char *deref_var(const char *m, func f,
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
		return deref_arr(m, f, vartypes, etemp);
	// Member access
	case 1: {
		size_t l = strlen(type);
		if (type[l - 1] == ']') {
			if (type[l - 2] == '[') {
				if (streq(member, "length")) {
					// Dynamic array
					const char *pointer = new_temp_var(f, "long*", "pointer");
					line_math(f, MATH_SUB, pointer, parent, "8");
					const char *length  = new_temp_var(f, "long", "length");
					line_math(f, MATH_LOADAT, length, pointer, "0");
					line_destroy(f, pointer);
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


static const char *parseexpr(const char *p, struct func *f, int *etemp,
                             const char *temptype, struct hashtbl *vartypes)
{
	const char *orgptr = p;
	// TODO
	if (strstart(p, "new ")) {
		// Forgive me for I am lazy
		const char *v = new_temp_var(f, strclone(p + 4), "new");
		const char *l = strnclone(strchr(p, '[') + 1, 1); // *puke*
		const char *a[1] = { l };
		line_function(f, v, "alloc", 1, a);
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
		return deref_var(strclone(words[0]), f, vartypes, etemp);
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

		const char *ret;
		if (precdl >= precdr) {
			const char *v = strclone(var);
			line_declare(f, v, temptype);
			h_add(vartypes, v, (size_t)temptype);

			const char *y = strclone(words[0]),
			           *z = strclone(words[2]);
			if (swap)
				SWAP(const char *, y, z);
			line_math(f, op, v, y, z);

			if (words[1][0] == '=') // meh
				line_math(f, MATH_INV, v, v, NULL);

			// Dragons
			size_t ok = f->linecount;
			const char *e = parseexpr(p, f, NULL, temptype, vartypes);
			fl.line = f->lines[ok + 1]; // + 1 to skip DECLARE
			fl.m->y = v;

			ret = e;
			if (!isnum(*v))
				line_destroy(f, v); 
		} else {
			int e0, e1;
			const char *z = parseexpr(p, f, &e0, temptype, vartypes);

			const char *v = strclone(var);
			line_declare(f, v, temptype);
			h_add(vartypes, v, (size_t)temptype);

			const char *y = deref_var(strclone(words[0]), f, vartypes, &e1);
			if (swap) {
				SWAP(const char *, y, z);
				SWAP(int, e0, e1);
			}
			line_math(f, op, v, y, z);
			if (e0)
				line_destroy(f, y);
			if (e1)
				line_destroy(f, z);

			ret = v;
			if (words[1][0] == '=') // meh
				line_math(f, MATH_INV, v, v, NULL);
		}
		return ret;
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



static void _parsefunc(func f, const char *ptr, const char *var,
                       hashtbl functbl, hashtbl vartypes)
{
	const char *c = ptr;
	while (*c != ' ' && *c != 0)
		c++;
	const char *name = strnclone(ptr, c - ptr);
	func g;
	if (h_get2(functbl, name, (size_t *)&g) == -1) {
		ERROR("Function '%s' not declared", name);
		EXIT(1);
	}
	if (*c != 0) {
		size_t      argcount = 0;
		const char *args [32];
		int         etemp[32];
		c++;
		while (1) {
			ptr = c;
			while (*c != ',' && *c != 0)
				c++;
			char b[2048];
			memcpy(b, ptr, c - ptr);
			b[c - ptr] = 0;
			const char *type = g->args[argcount].type;
			args[argcount] = parseexpr(b, f, &etemp[argcount], type, vartypes);
			if (!etemp[argcount])
				args[argcount] = strclone(args[argcount]);
			argcount++;
			if (*c == 0)
				break;
			c++;
		}
		const char **a = args;
		line_function(f, var, name, argcount, a);
		for (ssize_t i = argcount - 1; i >= 0; i--) {
			if (etemp[i])
				line_destroy(f, args[i]);
		}
	}
}



int parsefunc_header(struct func *f, const line_t line, const char *text)
{
	size_t i = 0, j = 0;
	const char *t = line.text;

	f->linecount = 0;
	f->linecap   = 32;
	f->lines     = malloc(f->linecap * sizeof *f->lines);
	if (f->lines == NULL)
		EXITERRNO("Failed to allocate lines array", 3);

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


void lines2func(const line_t *lines, size_t linecount,
                struct func *f, struct hashtbl *functbl)
{
	size_t li = 0;
	line_t line = lines[li];
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
				line_goto(f, loopends[l]->label);
			} else {
				ERROR("'break' outside loop");
				EXIT(1);
			}
		} else if (streq(word, "end")) {
			if (loopcount > 0) {
				loopcount--;
				if (loopelse[loopcount] != NULL) {
					switch (looptype[loopcount]) {
					case 1:
						insert_line(f, (struct func_line *)formath[loopcount]);
					case 2:
						insert_line(f, (struct func_line *)loopjmps[loopcount]);
					case 3:
						if (looptype[loopcount] == 1 &&
						    !isnum(*loopvars[loopcount]))
							line_destroy(f, loopvars[loopcount]);
						insert_line(f, (struct func_line *)loopelse[loopcount]);
						break;
					default:
						ERROR("Invalid loop type (%d)", looptype[loopcount]);
						EXIT(1);
					}
				}
				insert_line(f, (struct func_line *)loopends[loopcount]);
			} else {
				break;
			}
		} else if (streq(word, "else")) {
			if (loopcount > 0) {
				loopcount--;
				switch (looptype[loopcount]) {
				case 1:
					insert_line(f, (struct func_line *)formath[loopcount]);
				case 2:
					insert_line(f, (struct func_line *)loopjmps[loopcount]);
				case 3:
					if (looptype[loopcount] == 1 &&
					    !isnum(*loopvars[loopcount]))
						line_destroy(f, loopvars[loopcount]);
					insert_line(f, (struct func_line *)loopelse[loopcount]);
					loopelse[loopcount] = NULL;
					break;
				default:
					ERROR("Invalid loop type (%d)", looptype[loopcount]);
					EXIT(1);
				}
				loopcount++;
			} else {
				ERROR("Unexpected 'else'");
				EXIT(1);
			}

		} else if (streq(word, "for")) {

			NEXTWORD;

			const char *var = strclone(word);
			const char *iterator = var;

			NEXTWORD;

			if (!streq(word, "in")) {
				ERROR("Expected 'in', got '%s'", word);
				EXIT(1);
			}

			const char *p = ptr;
			const char *fromarray = NULL;
			const char *fromval, *toval;
			while (!strstart(ptr, " to ")) {
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

			fromval = parseexpr(word, f, NULL, "long", &vartypes);

			NEXTWORD;
			NEXTWORD;

			toval = parseexpr(ptr, f, NULL, "long", &vartypes);

		isfromarray:
			// Set iterator
			line_declare(f, iterator, "long");
			h_add(&vartypes, iterator, (size_t)"long");

			line_assign(f, iterator, fromval);

			// Create the labels
			char lbl[16], lbll[16], lble[16];
		       	snprintf(lbl , sizeof lbl , ".for_%d"     , loopcounter);
		       	snprintf(lbll, sizeof lbll, ".for_%d_else", loopcounter);
		       	snprintf(lble, sizeof lble, ".for_%d_end" , loopcounter);

			// Indicate the start of the loop
			line_label(f, strclone(lbl));

			// Test ending condition (fromval == toval)
			fl.i = calloc(sizeof *fl.i, 1);
			char expr[64];
			snprintf(expr, sizeof expr, "%s == %s", iterator, toval);
			const char *e = parseexpr(expr, f, NULL, "long", &vartypes);
			line_if(f, e, strclone(lbll));

			// Set variable to be used by the inner body of the loop
			// Only applies if fromarray is set
			if (fromarray != NULL)
				line_math(f, MATH_LOADAT, var, fromarray, iterator);

			// Destroy redundant testing variable
			line_destroy(f, e);

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
			
			line_label(f, lblc);

			const char *v = parseexpr(ptr, f, NULL, "bool", &vartypes);
			char expr[64];
			snprintf(expr, sizeof expr, "%s == 0", v);
			const char *e = parseexpr(expr, f, NULL, "bool", &vartypes);
			line_if(f, e, lbllc);
			if (!isnum(*e))
				line_destroy(f, e);

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

			const char *e = parseexpr(expr, f, NULL, "bool", &vartypes);
			line_if(f, e, lbllc);

			if (!isnum(*e))
				line_destroy(f, e);

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
			line_return(f, strclone(ptr));
		} else if (
			streq(word, "bool") || streq(word, "byte") || streq(word, "byte[21]") ||
			streq(word, "byte[4096]") ||
			streq(word, "long") || /* TODO */ streq(word, "byte[]")) {
			char *type = strclone(word);
			NEXTWORD;
			char *name = strclone(word);
			line_declare(f, name, type);
			h_add(&vartypes, name, (size_t)type);
		} else if (h_get(functbl, word) != -1) {
			_parsefunc(f, oldptr, NULL, functbl, &vartypes);
		} else {
			char name[32];
			strcpy(name, word);
			NEXTWORD;

			if (streq(word, "=")) {
				const char *p = ptr;
				NEXTWORD;
				if (h_get(functbl, word) != -1) {
					_parsefunc(f, p, strclone(name), functbl, &vartypes); 
				} else {
					int etemp;
					const char *type;
					const char *arr, *index;
					const char *v, *e;
					if (is_array_dereference(name, &arr, &index)) {
						if (h_get2(&vartypes, arr, (size_t *)&type) < 0) {
							ERROR("Variable '%s' not declared", arr);
							EXIT(1);
						}
						const char *de    = strchr(type, '[');
						const char *dtype = strnclone(type, de - type);
						v = arr;
						e = parseexpr(p, f, &etemp, dtype, &vartypes);
						line_store(f, arr, index, e);
					} else {
						if (h_get2(&vartypes, name, (size_t *)&type) < 0) {
							ERROR("Variable '%s' not declared", name);
							EXIT(1);
						}
						v = strclone(name);
						e = parseexpr(p, f, &etemp, type, &vartypes);
						line_assign(f, v, e);
					}
					// Destroy expression variable if newly allocated
					if (etemp)
						line_destroy(f, e);
				}
			} else {
				ERROR("Undefined thing '%s'", name);
				EXIT(1);
			}
		}

		// NEXT PLEASE!!!
		li++;
		line = lines[li];
	}
}

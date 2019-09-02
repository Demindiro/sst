#include "lines2func.h"
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "expr.h"
#include "func.h"
#include "hashtbl.h"
#include "util.h"
#include "var.h"
#include "vasm.h"



static int is_array_dereference(const char *str, const char **arr, const char **index)
{
	const char *p = strchr(str, '[');
	if (p != NULL) {
		p++;
		const char *q = strchr(p, ']');
		if (q == NULL)
			EXIT(1, "Expected ']'");
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
	if (h_get2(functbl, name, (size_t *)&g) == -1)
		EXIT(1, "Function '%s' not declared", name);
	size_t      argcount = 0;
	const char *args [32];
	char        etemp[32];
	if (*c != 0) {
		c++;
		while (1) {
			ptr = c;
			while (*c != ',' && *c != 0)
				c++;
			char b[2048];
			memcpy(b, ptr, c - ptr);
			b[c - ptr] = 0;
			const char *type = g->args[argcount].type;
			args[argcount] = parse_expr(f, b, &etemp[argcount], type, vartypes);
			if (!etemp[argcount])
				args[argcount] = strclone(args[argcount]);
			argcount++;
			if (*c == 0)
				break;
			c++;
		}
	}
	const char **a = args;
	line_function(f, var, name, argcount, a);
	for (size_t i = argcount - 1; i != -1; i--) {
		if (etemp[i])
			line_destroy(f, args[i]);
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
			EXIT(1, "");
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
			EXIT(1, "");
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
		EXIT(1, "");
	}
	j++;
	size_t k = 0;
	// No arguments
	if (t[j] == ')')
		goto noargs;
	for ( ; ; ) {
		i = j;
		// Get type
		while (t[j] != ' ')
			j++;
		f->args[k].type = strnclone(t + i, j - i);
		j++;
		i = j;
		// Get name
		while (t[j] != ',' && t[j] != ')')
			j++;
		f->args[k].name = strnclone(t + i, j - i);
		k++;
		// Argument list done
		if (t[j] == ')')
			break;
		j++;
	}
noargs:
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
			if (l >= 0)
				line_goto(f, loopends[l]->label);
			else
				EXIT(1, "'break' outside loop");
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
						EXIT(1, "Invalid loop type (%d)", looptype[loopcount]);
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
					goto noend;
				case 3:
					DEBUG("GOTO      '%s'", loopends[loopcount]->label);
					line_goto(f, loopends[loopcount]->label);
				noend:
					if (looptype[loopcount] == 1 &&
					    !isnum(*loopvars[loopcount]))
						line_destroy(f, loopvars[loopcount]);
					insert_line(f, (struct func_line *)loopelse[loopcount]);
					loopelse[loopcount] = NULL;
					break;
				default:
					EXIT(1, "Invalid loop type (%d)", looptype[loopcount]);
				}
				loopcount++;
			} else {
				EXIT(1, "Unexpected 'else'");
			}

		} else if (streq(word, "for")) {

			NEXTWORD;

			const char *var = strclone(word);
			const char *iterator = var;

			NEXTWORD;

			if (!streq(word, "in"))
				EXIT(1, "Expected 'in', got '%s'", word);

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

			char istemp;
			fromval = parse_expr(f, word, &istemp, "long", &vartypes);
			//if (istemp)
			//	line_destroy(f, fromval);

			NEXTWORD;
			NEXTWORD;

			toval = parse_expr(f, ptr, &istemp, "long", &vartypes);

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
			const char *e = parse_expr(f, expr, &istemp, "long", &vartypes);
			line_if(f, e, strclone(lbll));
			if (istemp)
				line_destroy(f, e);

			// Set variable to be used by the inner body of the loop
			// Only applies if fromarray is set
			if (fromarray != NULL) {
				line_declare(f, var, NULL); // TODO
				line_math(f, MATH_LOADAT, var, fromarray, iterator);
			}

			// Set loop incrementer
			formath[loopcount] = calloc(sizeof *formath[loopcount], 1);
			formath[loopcount]->type = MATH;
			formath[loopcount]->op        = MATH_ADD;
			formath[loopcount]->x         = iterator;
			formath[loopcount]->y         = iterator;
			formath[loopcount]->z         = "1";

			// Set loop repeat goto
			loopjmps[loopcount] = calloc(sizeof *loopjmps[loopcount], 1);
			loopjmps[loopcount]->type = GOTO;
			loopjmps[loopcount]->label     = strclone(lbl);

			// Set label for if the loop ended without interruption (break)
			loopelse[loopcount] = calloc(sizeof *loopelse[loopcount], 1);
			loopelse[loopcount]->type = LABEL;
			loopelse[loopcount]->label     = strclone(lbll);

			// Set label for if the loop was interrupted
			loopends[loopcount] = calloc(sizeof *loopends[loopcount], 1);
			loopends[loopcount]->type = LABEL;
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

			char istemp; // TODO
			const char *e = parse_expr(f, ptr, &istemp, "bool", &vartypes);
			line_if(f, e, lbllc);
			((struct func_line_if *)f->lines[f->linecount - 1])->inv = 1;
			if (istemp)
				line_destroy(f, e);

			flp.g = calloc(sizeof *flp.g, 1);
			flp.g->type    = GOTO;
			flp.g->label        = lblc;
			loopjmps[loopcount] = flp.g;

			loopelse[loopcount] = calloc(sizeof *loopelse[loopcount], 1);
			loopelse[loopcount]->type = LABEL;
			loopelse[loopcount]->label     = lbllc;

			loopends[loopcount] = calloc(sizeof *loopends[loopcount], 1);
			loopends[loopcount]->type = LABEL;
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

			char istemp;
			const char *e = parse_expr(f, ptr, &istemp, "bool", &vartypes);
			line_if(f, e, lbllc);
			((struct func_line_if *)f->lines[f->linecount - 1])->inv = 1;
			if (istemp)
				line_destroy(f, e);

			loopelse[loopcount] = calloc(sizeof *loopelse[loopcount], 1);
			loopelse[loopcount]->type = LABEL;
			loopelse[loopcount]->label     = lbllc;

			loopends[loopcount] = calloc(sizeof *loopends[loopcount], 1);
			loopends[loopcount]->type = LABEL;
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
					char etemp;
					const char *type;
					const char *arr, *index;
					const char *v, *e;
					if (is_array_dereference(name, &arr, &index)) {
						if (h_get2(&vartypes, arr, (size_t *)&type) < 0)
							EXIT(1, "Variable '%s' not declared", arr);
						const char *de    = strchr(type, '[');
						const char *dtype = strnclone(type, de - type);
						v = arr;
						e = parse_expr(f, p, &etemp, dtype, &vartypes);
						line_store(f, arr, index, e);
					} else {
						if (h_get2(&vartypes, name, (size_t *)&type) < 0)
							EXIT(1, "Variable '%s' not declared", name);
						v = strclone(name);
						e = parse_expr(f, p, &etemp, type, &vartypes);
						line_assign(f, v, e);
					}
					// Destroy expression variable if newly allocated
					if (etemp)
						line_destroy(f, e);
				}
			} else {
				EXIT(1, "Undefined thing '%s'", name);
			}
		}

		// NEXT PLEASE!!!
		li++;
		line = lines[li];
	}
}

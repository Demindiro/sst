#include "lines2func.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "expr.h"
#include "func.h"
#include "hashtbl.h"
#include "types.h"
#include "util.h"
#include "var.h"



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


static int is_member_dereference(const char *str, const char **parent, const char **member)
{
	const char *p = strchr(str, '.');
	if (p != NULL) {
		*parent = strnclone(str, p - str);
		*member = strclone(p + 1);
		return 1;
	}
	return 0;
}




static void _parsefunc(func f, const char *ptr, const char *var,
                       hashtbl variables)
{
	const char *c = ptr;
	while (*c != ' ' && *c != 0)
		c++;
	const char *name = strnclone(ptr, c - ptr);
	func g = get_function(name, variables);
	if (g == NULL)
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
			args[argcount] = parse_expr(f, b, &etemp[argcount], type, variables);
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
			line_destroy(f, args[i], variables);
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
		EXITERRNO(3, "Failed to allocate lines array");

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
                func f, const char *text)
{
	FDEBUG("Converting to immediate");

	size_t li = 0;
	line_t line = lines[li];
	struct hashtbl variables;
	h_create(&variables, 16);

	struct {
		const char *iterator;
		const char *incrementer;
		const char *jmps;
		const char *vars;
		const char *_else;
		const char *ends;
		int type;
	} loop[16];
	int loopcounter = 0;
	int loopcount   = 0;

	// Add function parameters to variables
	for (size_t i = 0; i < f->argcount; i++) {
		h_add(&variables, f->args[i].name, (size_t)f->args[i].type);
		// Structs are annoying
		struct type t;
		if (get_type(&t, f->args[i].type) < 0)
			EXIT(1, "Type '%s' not declared", f->args[i].type);
		if (t.type == TYPE_STRUCT) {
			struct type_meta_struct *m = (void *)&t.meta;
			for (size_t j = 0; j < m->count; j++) {
				const char *name = strprintf("%s@%s",
						f->args[i].name, m->names[j]);
				h_add(&variables, name, (size_t)strclone(m->types[j]));
			}
		}
	}

	if (f->functype == FUNC_CLASS) {
		// If the function is a constructor for a class, allocate some memory
		size_t size;
		if (get_type_size(f->type, &size) < 0)
			EXIT(3, "But how?");
		const char *arg = strprintf("%lu", size);
		line_declare(f, "this", f->name, &variables);
		line_function(f, "this", "__alloc", 1, &arg);
	} else if (f->functype == FUNC_STRUCT) {
		// If the function is a constructor, declare 'this"
		line_declare(f, "this", f->name, &variables);
	}

	for ( ; ; ) {

#define END_FOR   1
#define END_WHILE 2
#define END_IF    3

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
		
		// Determine line type
		if (streq(word, "break")) {
			size_t l = loopcount - 1;
			while (l >= 0 && loop[l].type == END_IF)
				l--;
			if (l >= 0)
				line_goto(f, loop[l].ends);
			else
				EXIT(1, "'break' outside loop");
		} else if (streq(word, "end")) {
			if (loopcount > 0) {
				loopcount--;
				if (loop[loopcount]._else != NULL) {
					switch (loop[loopcount].type) {
					case END_FOR:
						; const char *i = loop[loopcount].iterator   ,
						             *c = loop[loopcount].incrementer;
						line_math(f, MATH_ADD, i, i, c);
					case END_WHILE:
						line_goto(f, loop[loopcount].jmps);
					case END_IF:
						if (loop[loopcount].type == END_FOR &&
						    !isnum(*loop[loopcount].vars))
							line_destroy(f, loop[loopcount].vars, &variables);
						line_label(f, loop[loopcount]._else);
						break;
					default:
						EXIT(3, "Invalid loop type (%d)", loop[loopcount].type);
					}
				}
				line_label(f, loop[loopcount].ends);
			} else {
				break;
			}
		} else if (streq(word, "else")) {
			if (loopcount > 0) {
				loopcount--;
				switch (loop[loopcount].type) {
				case END_FOR:
					; const char *i = loop[loopcount].iterator   ,
					             *c = loop[loopcount].incrementer;
					line_math(f, MATH_ADD, i, i, c);
				case END_WHILE:
					line_goto(f, loop[loopcount].jmps);
					goto noend;
				case END_IF:
					line_goto(f, loop[loopcount].ends);
				noend:
					if (loop[loopcount].type == 1 &&
					    !isnum(*loop[loopcount].vars))
						line_destroy(f, loop[loopcount].vars, &variables);
					line_label(f, loop[loopcount]._else);
					loop[loopcount]._else = NULL;
					break;
				default:
					EXIT(3, "Invalid loop type (%d)", loop[loopcount].type);
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
					fromval   = "0";
					toval     = strprintf("%s.length", p);
					static size_t iteratorcount = 0;
					iterator = strprintf("_for_iterator_%lu", iteratorcount);;
					iteratorcount++;
					goto isfromarray;
				}
			}
			memcpy(word, p, ptr - p);
			word[ptr - p] = 0;

			char istemp;
			fromval = parse_expr(f, word, &istemp, "long", &variables);
			//if (istemp)
			//	line_destroy(f, fromval);

			NEXTWORD;
			NEXTWORD;

			toval = parse_expr(f, ptr, &istemp, "long", &variables);

		isfromarray:
			// Set iterator
			line_declare(f, iterator, "long", &variables);

			line_assign(f, iterator, fromval);

			// Create the labels
		       	const char *lbl  = strprintf(".for_%d"     , loopcounter),
		       	           *lbll = strprintf(".for_%d_else", loopcounter),
		       	           *lble = strprintf(".for_%d_end" , loopcounter);

			// Indicate the start of the loop
			line_label(f, lbl);

			// Test ending condition (fromval == toval)
			char expr[64];
			snprintf(expr, sizeof expr, "%s == %s", iterator, toval);
			const char *e = parse_expr(f, expr, &istemp, "long", &variables);
			line_if(f, e, strclone(lbll), 0);
			if (istemp)
				line_destroy(f, e, &variables);

			// Set variable to be used by the inner body of the loop
			// Only applies if fromarray is set
			if (fromarray != NULL) {
				// Get type
				const char *type; 
				if (h_get2(&variables, fromarray, (size_t *)&type) == -1)
					EXIT(1, "Variable '%s' not declared", fromarray);
				// Extract 'dereferenced' type
				const char *tq = strrchr(type, '[');
				if (tq == NULL)
					EXIT(1, "Variable '%s' of type '%s' cannot be iterated", fromarray, type);
				type = strnclone(type, tq - type);
				// Add lines
				line_declare(f, var, type, &variables);
				line_math(f, MATH_LOADAT, var, fromarray, iterator);
			}

			// Set loop incrementer
			loop[loopcount].iterator    = iterator;
			loop[loopcount].incrementer = "1";

			// Set loop repeat goto
			loop[loopcount].jmps = lbl;

			// Set label for if the loop ended without interruption (break)
			loop[loopcount]._else = lbll;

			// Set label for if the loop was interrupted
			loop[loopcount].ends = lble;

			// Set the loop type
			loop[loopcount].type = END_FOR;
			loop[loopcount].vars = toval;

			// Increment counters
			loopcount++;
			loopcounter++;
		} else if (streq(word, "while")) {
			const char *lbl  = strprintf(".while_%d"     , loopcounter),
			           *lbll = strprintf(".while_%d_else", loopcounter),
			           *lble = strprintf(".while_%d_end" , loopcounter);
			
			line_label(f, lbl);

			char istemp;
			const char *e = parse_expr(f, ptr, &istemp, "bool", &variables);
			line_if(f, e, lbll, 1);
			if (istemp)
				line_destroy(f, e, &variables);

			loop[loopcount].jmps  = lbl;
			loop[loopcount]._else = lbll;
			loop[loopcount].ends  = lble;
			loop[loopcount].type  = END_WHILE;

			loopcount++;
			loopcounter++;
		} else if (streq(word, "if")) {
			const char *lbll = strprintf(".if_%d_else", loopcounter),
			           *lble = strprintf(".if_%d_end" , loopcounter);

			char istemp;
			const char *e = parse_expr(f, ptr, &istemp, "bool", &variables);
			line_if(f, e, lbll, 1);
			if (istemp)
				line_destroy(f, e, &variables);

			loop[loopcount]._else = lbll;
			loop[loopcount].ends  = lble;
			loop[loopcount].type  = END_IF;

			loopcount++;
			loopcounter++;
		} else if (streq(word, "return")) {
			char istemp;
			const char *e = parse_expr(f, ptr, &istemp, f->type, &variables);
			line_return(f, e);
			if (istemp)
				line_destroy(f, e, &variables);
		} else if (streq(word, "throw")) {
			line_throw(f, ptr);
		} else if (streq(word, "__asm")) {
			const char *invars[32] , *outvars[32] ;
			char        inregs[32] ,  outregs[32] ;
			size_t      incount = 0,  outcount = 0;
			const char**vasms = malloc(64 * sizeof *vasms);
			size_t      vasmcount = 0;
			const char *l, *t;
			// Get input vars
			t = l = lines[++li].text;
			while (1) {
				// Extract argument
				const char *lp = l;
				while (*lp != ',' && *lp != 0)
					lp++;
				char b[256];
				memcpy(b, l, lp - l);
				b[lp - l] = 0;
				// Get register
				char *p = b, *q = p;
				if (*q != 'r') {
					PRINTLINEX(lines[li], l - t, text);
					EXIT(1, "Registers must start with a 'r'");
				}
				q++;
				p = q;
				while (*q != ' ') {
					if (!isnum(*q)) {
						PRINTLINEX(lines[li], l - t, text);
						EXIT(1, "Expected number");
					}
					q++;
				}
				*q = 0;
				long r = strtol(p, NULL, 0);
				if (r >= 32) {
					PRINTLINEX(lines[li], l - t, text);
					EXIT(1, "There are only 32 registers defined in the SS ISA");
				}
				inregs[incount] = r;
				// Check for equal sign
				q++; // ' '
				if (*q != '=') {
					PRINTLINEX(lines[li], l - t, text);
					EXIT(1, "Expected '='");
				}
				q += 2; // '= '
				// Copy var
				p = q;
				while (*q != 0) {
					if (*q == 0) {
						PRINTLINEX(lines[li], l - t, text);
						EXIT(1, "Expected '='");
					}
					q++;
				}
				invars[incount] = strnclone(p, q - p);
				// Next
				incount++;
				if (*lp == 0)
					break;
				l = lp + 1;
			}
			// Get output vars
			t = l = lines[++li].text;
			while (1) {
				// Extract argument
				const char *lp = l;
				while (*lp != ',' && *lp != 0)
					lp++;
				char b[256];
				memcpy(b, l, lp - l);
				b[lp - l] = 0;
				// Copy var
				char *p = b, *q = p;
				while (*q != ' ') {
					if (*q == 0) {
						PRINTLINEX(lines[li], lp - t, text);
						EXIT(1, "Expected '='");
					}
					q++;
				}
				outvars[outcount] = strnclone(p, q - p);
				// Check for equal sign
				q++; // ' '
				if (*q != '=') {
					PRINTLINEX(lines[li], lp - t, text);
					EXIT(1, "Expected '='");
				}
				q += 2; // '= '
				// Get register
				if (*q != 'r') {
					PRINTLINEX(lines[li], lp - t + q - p + 1, text);
					EXIT(1, "Registers must start with a 'r'");
				}
				q++;
				p = q;
				while (*q != 0) {
					if (!isnum(*q)) {
						PRINTLINEX(lines[li], lp - t, text);
						EXIT(1, "Expected number");
					}
					q++;
				}
				*q = 0;
				long r = strtol(p, NULL, 0);
				if (r >= 32) {
					PRINTLINEX(lines[li], lp - t, text);
					EXIT(1, "There are only 32 registers defined in the SS ISA");
				}
				outregs[outcount] = r;
				// Next
				outcount++;
				if (*lp == 0)
					break;
				l = lp + 1;
			}
			// Insert ops
			li++;
			while (!streq(lines[li].text, "end"))
				vasms[vasmcount++] = lines[li++].text;
			line_asm(f, vasms, vasmcount, invars, inregs, incount, outvars, outregs, outcount);
		} else if (is_type(word)) {
			char *type = strclone(word);
			NEXTWORD;
			char *name = strclone(word);
			line_declare(f, name, type, &variables);
			h_add(&variables, name, (size_t)type);
		} else if (get_function(word, &variables) != NULL) {
			_parsefunc(f, oldptr, NULL, &variables);
		} else {
			const char *name = strclone(word);
			NEXTWORD;

			if (streq(word, "=")) {
				const char *p = ptr;
				NEXTWORD;
				if (get_function(word, &variables) != NULL) {
					const char *tmp = new_temp_var(f, "long", "todo", &variables); // TODO
					line_function_parse(f, tmp, p, &variables); 
					assign_var(f, name, tmp, &variables);
					line_destroy(f, tmp, &variables);
				} else {
					char etemp;
					const char *type;
					const char *arr, *index;
					const char *parent, *member;
					const char *e;
					if (is_array_dereference(name, &arr, &index)) {
						if (h_get2(&variables, arr, (size_t *)&type) < 0)
							EXIT(1, "Variable '%s' not declared", arr);
						const char *de    = strchr(type, '[');
						const char *dtype = strnclone(type, de - type);
						e = parse_expr(f, p, &etemp, dtype, &variables);
						line_store(f, arr, index, e);
					} else if (is_member_dereference(name, &parent, &member)) {
						if (h_get2(&variables, parent, (size_t *)&type) < 0)
							EXIT(1, "Variable '%s' not declared", parent);
						struct type t;
						if (get_type(&t, type) < 0)
							EXIT(3, "Type '%s' doesn't exist", type);
						if (t.type == TYPE_STRUCT) {
							struct type_meta_struct *m = (void *)&t.meta;
							const char *dtype;
							for (size_t i = 0; i < m->count; i++) {
								if (streq(m->names[i], member)) {
									dtype = m->types[i];
									goto found_s;
								}
							}
							EXIT(1, "Struct '%s' doesn't have member '%s'", parent, member);
						found_s:
							e = parse_expr(f, p, &etemp, dtype, &variables);
							// Structs ought to be stored in the registers or on
							// the stack, so let func2vasm deal with "dereferencing"
							//line_assign(f, name, e);
							assign_var(f, name, e, &variables);
						} else if (t.type == TYPE_CLASS) {
							struct type_meta_class *m = (void *)&t.meta;
							const char *dtype;
							for (size_t i = 0; i < m->count; i++) {
								if (streq(m->names[i], member)) {
									dtype = m->types[i];
									goto found_c;
								}
							}
							EXIT(1, "Class '%s' doesn't have member '%s'", parent, member);
						found_c:
							e = parse_expr(f, p, &etemp, dtype, &variables);
							size_t o;
							if (get_member_offset(t.name, member, &o) < 0)
								EXIT(1, "Type '%s' is not declared", t.name);
							line_store(f, parent, strprintf("%lu", o), e);
						} else {
							EXIT(1, "Type '%s' is not a struct or class", type);
						}
					} else {
						if (h_get2(&variables, name, (size_t *)&type) < 0) {
							PRINTLINEX(line, 0, text);
							EXIT(1, "Variable '%s' not declared", name);
						}
						e = parse_expr(f, p, &etemp, type, &variables);
						line_assign(f, name, e);
					}
					// Destroy expression variable if newly allocated
					if (etemp)
						line_destroy(f, e, &variables);
				}
			} else if (strchr(name, '[') != NULL) {
				const char *p = strchr(line.text, '[');
				name = strnclone(line.text, p - line.text);
				p++;
				const char *q = strchr(p, ']');
				char e[1024];
				memcpy(e, p, q - p);
				e[q - p] = 0;
				char tempi, tempv;
				p = q + 4;
				const char *i = parse_expr(f, e, &tempi, "long", &variables);
				const char *v = parse_expr(f, p, &tempv, "long", &variables);
				line_store(f, name, i, v);
				if (tempi)
					line_destroy(f, i, &variables);
				if (tempv)
					line_destroy(f, v, &variables);
			} else {
				PRINTLINEX(line, ptr - oldptr, text);
				EXIT(1, "Undefined keyword '%s'", name);
			}
		}

		// NEXT PLEASE!!!
		li++;
		line = lines[li];
	}

	if (f->functype == FUNC_CLASS || f->functype == FUNC_STRUCT) {
		line_return(f, "this");
	}
}

#include <string.h>
#include <stdio.h>
#include "func.h"
#include "hashtbl.h"
#include "util.h"


static const char *deref_arr(const char *w, struct func *f,
                             struct hashtbl *vartypes, char *etemp)
{
	static int counter = 0;
	const char *p = strchr(w, '[');
	if (p != NULL) {
		// Get array and index
		p++;
		const char *q = strchr(p, ']');
		const char *array = strnclone(w, p - w - 1);
		const char *index = strnclone(p, q - p);
		// Get type
		const char *type;
		if (h_get2(vartypes, array, (size_t *)&type) == -1)
			EXIT(1, "Variable '%s' not declared", array);
		// Extract 'dereferenced' type
		const char *tq = strrchr(type, '[');
		if (tq == NULL)
			tq = strrchr(type, '*');
		if (tq == NULL)
			EXIT(1, "Variable '%s' of type '%s' cannot be dereferenced", array, type);
		type = strnclone(type, tq - type);
		// Create temporary variable
		char var[16];
		snprintf(var, sizeof var, "__elem%d", counter++);
		char *v = strclone(var);
		// Add lines
		line_declare(f, v, type);
		line_math(f, MATH_LOADAT, v, array, index);
		*etemp = 1;
		return v;
	}
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
const char *deref_var(const char *m, func f,
                      hashtbl vartypes, char *etemp)
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
			if (c == NULL)
				EXIT(1, "Expected ']'");
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
			EXIT(1, "Variable '%s' is not declared", var);
		}
	}

	switch (deref_type) {
	// No dereference
	case -1:
		if (etemp)
			*etemp = 0;
		return strclone(m);
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
					EXIT(1, "Dynamic array '%s' doesn't have member '%s'", parent, member);
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
					EXIT(1, "Fixed array '%s' doesn't have member '%s'", parent, member);
				}
			}
		} else {
			EXIT(1, "'%s' doesn't have member '%s'", parent, member);
		}
	}
	}
	EXIT(3, "This point should be unreachable");
}

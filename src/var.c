#include <string.h>
#include <stdio.h>
#include "func.h"
#include "hashtbl.h"
#include "types.h"
#include "util.h"
#include "var.h"
#include "expr.h"


static const char *_deref_arr(const char *w, struct func *f,
                              hashtbl variables, char *etemp)
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
		if (h_get2(variables, array, (size_t *)&type) == -1)
			EXIT(1, "Variable '%s' not declared", array);
		// Extract 'dereferenced' type
		const char *tq = strrchr(type, '[');
		if (tq == NULL)
			tq = strrchr(type, '*');
		if (tq == NULL)
			EXIT(1, "Variable '%s' of type '%s' cannot be dereferenced", array, type);
		type = strnclone(type, tq - type);
		// Create temporary variable
		const char *v = strprintf("__elem%d", counter++);
		// If type size isn't 1, multiply the index by the size
		size_t size;
		if (get_type_size(type, &size) < 0)
			EXIT(1, "Type '%s' not declared", type);
		// Add lines
		if (size != 1) {
			const char *i = new_temp_var(f, "long", "i", variables);
			line_math(f, MATH_MUL, i, index, strprintf("%lu", size));
			line_declare(f, v, type, variables);
			line_math(f, MATH_LOADAT, v, array, i);
			line_destroy(f, i, variables);
		} else {
			line_declare(f, v, type, variables);
			line_math(f, MATH_LOADAT, v, array, index);
		}
		*etemp = 1;
		return v;
	}
	*etemp = 0;
	return w;
}



static int _split(const char *s, char *x, size_t xl, char *y, size_t yl, char c)
{
	const char *p = strchr(s, c);
	if (p == NULL) {
		strncpy(x, s, xl);
		x[xl - 1] = 0;
		*y = 0;
		return 0;
	} else {
		size_t l = p - s;
		l = l < (xl - 1) ? l : (xl - 1);
		memcpy(x, s, l);
		x[l] = 0;

		p++;
		const char *q = strchr(p, '.');
		if (q == NULL) {
			strncpy(y, p, yl);
			y[yl - 1] = 0;
		} else {
			size_t l = q - p;
			l = l < (yl - 1) ? l : (yl - 1);
			memcpy(y, s, l);
			y[l] = 0;
		}
		return 1;
	}
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
                      hashtbl variables, char *etemp)
{
	const char *c = m;
	const char *var;
	const char *array/*, *index*/;
	const char *parent;
	int deref_type = -1;

	// Check if there is anything to dereference
	const char *p = strchr(m, '.');
	if (p == NULL) {
		p = strchr(m, '[');
		if (p != NULL)
			return _deref_arr(m, f, variables, etemp);
		*etemp = 0;
		return m;
	}
	const char *member = p + 1;

	// Get type
	struct type type;
	if (get_root_type(&type, m, variables, &parent) == -1)
		EXIT(1, "Root variable in '%s' is not declared", m);


	switch (type.type) {
	case TYPE_NUMBER:
		; struct type_meta_number *mn = (void *)&type.meta;
		if (streq(member, "sizeof")) {
			*etemp = 0;
			return strprintf("%lu", mn->size);
		} else {
			EXIT(1, "Type number doesn't have member '%s'", member);
		}
		break;
	case TYPE_POINTER:
		EXIT(4, "TODO: pointers");
	case TYPE_ARRAY:
		; struct type_meta_array *ma = (void *)&type.meta;
		if (!ma->fixed) {
			if (streq(member, "length")) {
				// Dynamic array
				const char *pointer = new_temp_var(f, "long*", "ptr", variables);
				const char *length  = new_temp_var(f, "long", "len", variables);
				line_assign(f, pointer, parent);
				line_math(f, MATH_LOADAT, length, pointer, "-8");
				line_destroy(f, pointer, variables);
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
				if (etemp) *etemp = 0;
				return strprintf("%lu", ma->size);
			} else if (streq(member, "ptr")) {
				if (etemp) *etemp = 0;
				return parent;
			} else {
				EXIT(1, "Fixed array '%s' doesn't have member '%s'", parent, member);
			}
		}
	case TYPE_CLASS: {
		// Get offset
		char b[256];
		const char *c = strchr(member, '.');
		if (c == NULL) {
			strcpy(b, member);
		} else {
			memcpy(b, member, c - member);
			b[c - member] = 0;
		}
		const char *t = get_member_type(type.name, b);
		if (t == NULL) {
			func g = get_function(m, variables);
			if (g == NULL)
				EXIT(1, "Variable '%s' of class '%s' doesn't have member '%s'", m, type.name, b);
			const char *cvar = new_temp_var(f, g->type, NULL, variables);
			line_function_parse(f, cvar, m, variables);
			*etemp = 1;
			return cvar;
		}
		const char *cvar = new_temp_var(f, t, NULL, variables);
		size_t o;
		if (get_member_offset(type.name, b, &o) < 0)
			EXIT(3, "wtf?");
		line_load(f, cvar, parent, strprintf("%ld", o));
		const char *s = c == NULL ? cvar : strprintf("%s.%s", cvar, member);
		const char *rvar = deref_var(s, f, variables, etemp);
		if (*etemp)
			line_destroy(f, cvar, variables);
		else
			rvar = cvar;
		return rvar;
	}
	case TYPE_STRUCT: {
		// Eh
		char b[256];
		const char *c = strchr(member, '.');
		if (c == NULL) {
			strcpy(b, m);
		} else {
			memcpy(b, m, c - m);
			b[c - m] = 0;
		}
		*strchr(b, '.') = '@';
		const char *t;
		if (h_get2(variables, b, (size_t *)&t) < 0)
			EXIT(1, "Variable '%s' not declared", b);
		const char *s = c == NULL ? strclone(b) : strprintf("%s.%s", b, c + 1);
		return deref_var(s, f, variables, etemp);
	}
	default:
		EXIT(3, "Unknown type (%d)", type.type);
	}

	EXIT(3,"BAKANA   '%s'", type.name);


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
#define type _type
	const char *type;
	if (deref_type != -1) {
		if (h_get2(variables, var, (size_t *)&type) < 0) {
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
		return _deref_arr(m, f, variables, etemp);
	// Member access
	case 1: {
		size_t l = strlen(type);
		if (type[l - 1] == ']') {
		} else {
			EXIT(1, "'%s' doesn't have member '%s'", parent, member);
		}
	}
	}
	EXIT(3, "This point should be unreachable");
#undef type
}




int assign_var(func f, const char *var, const char *val, hashtbl variables)
{
	const char *typename;

	// Parse to-be-assigned value
	char tempdval;
	const char *dvaltype;
	const char *dval = parse_expr(f, val, &tempdval, "TODO", variables);

	// Find the first 'dereference symbol'
	const char *p = var;
	while (*p != 0) {
		if (*p == '[')
			goto array_dereference;
		else if (*p == '.')
			goto member_dereference;
		p++;
	}
	goto no_dereference;

array_dereference:;
	// Get the array name
	const char *array = strnclone(var, p - var);
	p++;
	// Get the index name
	const char *q = p;
	while (*p != ']') {
		if (*p == 0)
			EXIT(1, "Expected ']'");
		p++;
	}
	const char *index = strnclone(q, p - q);
	p++;
	// If the size isn't 1, multiply the index by the size
	if (h_get2(variables, array, (size_t *)&typename) == -1)
		EXIT(1, "Variable '%s' not declared", array);
	size_t size;
	if (get_deref_type_size(typename, &size) < 0)
		EXIT(1, "Type '%s' not declared", typename);
	const char *i;
	if (size != 1) {
		i = new_temp_var(f, "long", "i", variables);
		line_math(f, MATH_MUL, i, index, strprintf("%lu", size));
	} else {
		i = index;
	}
	// Access the array at 'i'
	if (*p == 0) {
		// If it is the last dereference, then store the value
		line_store(f, array, i, dval);
	} else {
		// Else we may have to perform magic with structs, so crash for now
		EXIT(4, "TODO: struct magic");
	}
	// Destroy the temporary 'i' index
	if (size != 1)
		line_destroy(f, i, variables);
	goto end;

member_dereference:;
	// Get the parent's name
	const char *parent = strnclone(var, p - var);
	p++;
	// Get the members name
	/*const char **/q = p;
	while (1) {
		if (*p == '.' || *p == '[' || *p == 0)
			break;
		p++;
	}
	const char *member = strnclone(q, p - q);
	// Check if it is the final dereference
	int isfinal = *p == 0;
	// Check what type the parent is
	struct type type;
	if (h_get2(variables, parent, (size_t *)&typename) < 0)
		EXIT(1, "Variable '%s' is not declared", parent);
	if (get_type(&type, typename) < 0)
		EXIT(1, "Undeclared type: '%s'", typename);
	if (type.type == TYPE_NUMBER) {
		// There are no mutable number properties
		EXIT(3, "Number properties are immutable");
	} else if (type.type == TYPE_POINTER) {
		// There are no mutable pointer properties
		EXIT(3, "Pointer properties are immutable");
	} else if (type.type == TYPE_ARRAY) {
		// There are no mutable array properties
		EXIT(3, "Array properties are immutable");
	} else if (type.type == TYPE_CLASS) {
		// Get the member's offset
		size_t o;
		if (get_member_offset(typename, member, &o) == -1)
			EXIT(1, "Class '%s' doesn't have member '%s'", parent, member);
		const char *so = strprintf("%ld", o);
		if (isfinal) {
			// Store the value
			line_store(f, parent, so, dval);
		} else {
			// Load the value, assign the value to it and store it again
			// (Easiest way to deal with structs right now)
			const char *mtype = get_member_type(typename, member);
			const char *tmp = new_temp_var(f, mtype, NULL, variables);
			line_load(f, tmp, parent, so);
			const char *n = strprintf("%s%s", tmp, p);
			assign_var(f, n, dval, variables);
			line_store(f, parent, so, tmp);
			line_destroy(f, tmp, variables);
		}
	} else if (type.type == TYPE_STRUCT) {
		if (isfinal) {
			// 'Merge' parent and member into one variable and assign it
			const char *n = strprintf("%s@%s", parent, member);
			line_assign(f, n, dval);
		} else {
			// Call assign_var with merged variable
			const char *n = strprintf("%s@%s", parent, q);
			assign_var(f, n, dval, variables);
		}
	} else {
		EXIT(3, "Unknown type (%d)", type.type);
	}
	goto end;

no_dereference:
	// Simply assign the value
	line_assign(f, var, dval);

end:
	// Destroy temporary values to free up registers
	if (tempdval)
		line_destroy(f, dval, variables);
	return 0;
}

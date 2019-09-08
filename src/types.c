#include "types.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "hashtbl.h"
#include "util.h"


static struct type *types;
static struct hashtbl typestable;
static size_t typescount, typescapacity;


static int _add_type(const char *name, struct type_meta *m, size_t ms,
                     enum type_type type)
{
	struct type t = { .name = name, .type = type };
	memcpy(&t.meta, m, ms);
	if (typescapacity <= typescount) {
		if (types == NULL)
			h_create(&typestable, 4);
		size_t n = typescapacity * 3 / 2 + 1;
		void  *a = realloc(types, n * sizeof *types);
		if (a == NULL)
			return -1;
		typescapacity = n;
		types = a;
	}
	types[typescount++] = t;
	return h_add(&typestable, name, typescount - 1);
}


int add_type_number(const char *name, size_t size, int _signed)
{
	struct type_meta_number m = { .size = size, ._signed = _signed };
	return _add_type(name, (struct type_meta *)&m, sizeof m, TYPE_NUMBER);
}


int add_type_class(const char *name, const char **names,
                   const char **types, size_t count)
{
	struct type_meta_class m = { .count = count };
	m.names = malloc(count * sizeof *m.names);
	m.types = malloc(count * sizeof *m.types);
	if (m.names == NULL || m.types == NULL) {
		free(m.names);
		free(m.types);
		return -1;
	}
	for (size_t i = 0; i < count; i++) {
		m.names[i] = names[i];
		m.types[i] = types[i];
	}
	return _add_type(name, (struct type_meta *)&m, sizeof m, TYPE_CLASS);
}


int add_type_struct(const char *name, const char **names,
                    const char **types, size_t count)
{
	struct type_meta_struct m = { .count = count };
	m.names = malloc(count * sizeof *m.names);
	m.types = malloc(count * sizeof *m.types);
	if (m.names == NULL || m.types == NULL) {
		free(m.names);
		free(m.types);
		return -1;
	}
	for (size_t i = 0; i < count; i++) {
		m.names[i] = names[i];
		m.types[i] = types[i];
	}
	return _add_type(name, (struct type_meta *)&m, sizeof m, TYPE_STRUCT);
}


int is_type(const char *name)
{
	struct type dummy;
	return get_type(&dummy, name) + 1;
}


int get_type(struct type *dest, const char *name)
{
	size_t l = strlen(name);
	char ddname[1024];

	if (l == 0)
		return -1;

	if (name[l - 1] == '*') {

		const char *p = strchr(name, '*');
		memcpy(ddname, name, p - name);
		ddname[p - name] = 0;
		size_t dummy;
		if (typescount == 0 || h_get2(&typestable, ddname, &dummy) < 0)
			return -1;

		dest->name = name;
		dest->type = TYPE_POINTER;
	} else if (name[l - 1] == ']') {
		struct type_meta_array *m = (void *)&dest->meta;

		size_t i = 1;
		while (name[i] != '[') {
			if (i == l)
				return -1;
			i++;
		}

		m->fixed = i != l - 2;
		if (m->fixed)
			m->size = atoi(name + i + 1);

		const char *p = strchr(name, '[');
		memcpy(ddname, name, p - name);
		ddname[p - name] = 0;
		size_t dummy;
		if (typescount == 0 || h_get2(&typestable, ddname, &dummy) < 0)
			return -1;

		dest->name = name;
		dest->type = TYPE_ARRAY;
	} else {
		size_t i = 0;
		if (typescount == 0 || h_get2(&typestable, name, &i) < 0)
			return -1;
		*dest = types[i];
	}

	return 0;
}


int get_deref_type(struct type *dest, const char *name)
{
	size_t l = strlen(name);
	char dname[256];

	if (l == 0)
		return -1;

	// Get dname
	if (name[l - 1] == ']') {
		if (l < 3) // Mininum 3 chars (e.g. 'a[]')
			return -1;
		size_t i = l - 2;
		while (name[i] != '[') {
			i--;
			if (i == 0)
				return -1;
		}
		memcpy(dname, name, i);
		dname[i] = 0;
		l = i;
	} else if (name[l - 1] == '*') {
		l--;
		memcpy(dname, name, l);
		dname[l] = 0;
	}

	// Check if dereferenced type is pointer or array
	if (dname[l - 1] == ']') {
		if (l < 3)
			return -1;
		dest->name = strclone(dname);
		dest->type = TYPE_ARRAY;
		struct type_meta_array *m = (void *)&dest->meta;
		m->fixed = dname[l - 2] != '[';
		if (m->fixed) {
			size_t i = l - 3;
			while (dname[i] != '[') {
				i--;
				if (i == l)
					return -1;
			}
			m->size = atoi(&dname[i + 1]);
		}
		return 0;
	} else if (dname[l - 1] == '*') {
		dest->name = strclone(dname);
		dest->type = TYPE_POINTER;
		return 0;
	}

	// Get non-pointer/array type
	size_t i;
	if (typescount == 0 || h_get2(&typestable, dname, &i) < 0)
		return -1;

	*dest = types[i];
	return 0;
}


int get_type_size(const char *name, size_t *size)
{
	size_t i;
	if (typescount == 0 || h_get2(&typestable, name, &i) < 0)
		return -1;
	struct type *t = &types[i];
	if (t->type == TYPE_NUMBER) {
		struct type_meta_number *m = (void *)&t->meta;
		*size = m->size;
	} else if (t->type == TYPE_ARRAY || t->type == TYPE_POINTER ||
	           t->type == TYPE_CLASS) {
		return 8;
	} else if (t->type == TYPE_STRUCT) {
		struct type_meta_struct *m = (void *)&t->meta;
		size_t s = 0;
		for (size_t i = 0; i < m->count; i++) {
			size_t x;
			if (get_type_size(m->types[i], &x) < 0)
				return -1;
			s += x;
		}
		*size = s;
	} else {
		return -1;
	}
	return 0;
}


int get_member_offset(const char *parent, const char *member, size_t *offset)
{
	size_t i;
	if (h_get2(&typestable, parent, &i) < 0)
		return -1;
	struct type *t = &types[i];
	if (t->type != TYPE_CLASS)
		return -1;
	struct type_meta_class *m = (void *)&t->meta;
	size_t o = 0;
	for (size_t j = 0; j < m->count; j++) {
		if (streq(member, m->names[j])) {
			*offset = o;
			return 0;
		}
		size_t s;
		if (get_type_size(m->types[j], &s) < 0)
			return -1;
		// Why not align, you ask?
		//  - x86 allows unaligned accesses
		//  - If the value is not on a cache boundary, there
		//    is no penalty (on modern processors)
		//  - If the value is on a cache boundary, there is
		//    a (nowadays small) penalty
		//  - The odds of a 8 byte value *not* being on the boundary
		//    of a 64 byte cache line is (64 - 8 + 1) / 64 = 0.89
		//  - Those odds are good enough
		o += s;
	}
	return -1;
}


const char *get_member_type(const char *parent, const char *member)
{
	size_t i;
	if (h_get2(&typestable, parent, &i) < 0)
		return NULL;
	struct type *t = &types[i];
	if (t->type != TYPE_CLASS)
		return NULL;
	struct type_meta_class *m = (void *)&t->meta;
	for (size_t j = 0; j < m->count; j++) {
		if (streq(member, m->names[j]))
			return m->types[j];
	}
	return NULL;
}



int get_root_type(struct type *dest, const char *str, hashtbl variables, const char **parent)
{
	const char *p = strchr(str, '.');
	size_t l = p == NULL ? strlen(str) : p - str;
	*parent = strnclone(str, l);
	const char *type;
	if (h_get2(variables, *parent, (size_t *)&type) < 0)
		return -1;
	return get_type(dest, type);
}


const char *get_function_name(const char *str, hashtbl variables)
{
	const char *name;
	struct type t;
	char var[256], func[256];

	const char *p = strchr(str, '.');
	if (p == NULL)
		return NULL;
	memcpy(var, str, p - str);
	var[p - str] = 0;
	strcpy(func, p + 1);


	if (h_get2(variables, var, (size_t *)&name) < 0)
		return NULL;
	if (get_type(&t, name) < 0)
		return NULL;
	if (t.type == TYPE_CLASS || t.type == TYPE_STRUCT)
		return strprintf("%s.%s", t.name, func);
	return NULL;
}

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


int add_type_class(const char *name, const char *names,
                   const char *types, size_t count)
{
	struct type_meta_class m = { .count = count };
	m.names = malloc(count * sizeof *m.names);
	m.types = malloc(count * sizeof *m.types);
	if (m.names == NULL || m.types == NULL) {
		free(m.names);
		free(m.types);
		return -1;
	}
	return _add_type(name, (struct type_meta *)&m, sizeof m, TYPE_CLASS);
}


int add_type_struct(const char *name, const char *names,
                    const char *types, size_t count)
{
	struct type_meta_struct m = { .count = count };
	m.names = malloc(count * sizeof *m.names);
	m.types = malloc(count * sizeof *m.types);
	if (m.names == NULL || m.types == NULL) {
		free(m.names);
		free(m.types);
		return -1;
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
		size_t i = 1;
		while (name[i] != '[') {
			if (i == l)
				return -1;
			i++;
		}

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
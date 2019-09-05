#ifndef TYPES_H
#define TYPES_H


enum type {
	TYPE_NUMBER,
	TYPE_ARRAY,
	TYPE_POINTER,
	TYPE_CLASS,
	TYPE_STRUCT,
};



struct type_meta {
	char _[32];
};


struct type_meta_number {
	size_t size;
	int _signed : 1;
};


struct type_meta_array {
	int fixed : 1;
	size_t size;
};


struct type_meta_pointer {
};


struct type_meta_class {
	size_t       count;
	const char **names;
	const char **types;
};


struct type_meta_struct {
	size_t       count;
	const char **names;
	const char **types;
};


struct type {
	const char      *name;
	enum type        type;
	struct type_meta meta;
};


/**
 * Adds a new number type.
 *
 * Note that to complete creation, process_types has to be called.
 * If process_types has already been called, create_type will only
 * create full types.
 * This is to solve cycles when first parsing classes and structs. This
 * problem doesn't occur when parsing functions.
 */
int add_type_number(const char *name, size_t size, int _signed);


/**
 * Adds a new class type
 */
int add_type_class(const char *name, const char *membernames,
                   const char *membertypes, size_t membercount);


/**
 * Adds a new struct type
 */
int add_type_struct(const char *name, const char *membernames,
                    const char *membertypes, size_t membercount);


/**
 * Gets a type.
 */
int get_type(struct type *dest, const char *name);


/**
 * Get a dereferenced type.
 */
int get_deref_type(struct type *dest, const char *name);


/**
 * Get a types size.
 */
int get_type_size(const char *name, size_t *i);


#endif

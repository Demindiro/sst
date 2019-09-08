#ifndef VAR_H
#define VAR_H


#include "func.h"
#include "hashtbl.h"


const char *deref_var(const char *m, func f,
                      hashtbl vartypes, char *etemp);


/**
 * Inserts statements to assign a value to a variable
 */
int assign_var(func f, const char *var, const char *val);


#endif

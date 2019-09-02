#ifndef VAR_H
#define VAR_H


#include "func.h"
#include "hashtbl.h"


const char *deref_var(const char *m, func f,
                      hashtbl vartypes, char *etemp);


#endif

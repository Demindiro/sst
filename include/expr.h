#ifndef EXPR_H
#define EXPR_H

#include "func.h"
#include "hashtbl.h"

const char *parse_expr(func f, const char *str, char *istemp, const char *type,
                       hashtbl vartypes);


#endif

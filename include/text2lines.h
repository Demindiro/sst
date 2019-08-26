#ifndef TEXT2LINES_H
#define TEXT2LINES_H

#include <lines.h>

int text2lines(const char *text,
               line_t **lines  , size_t *linecount,
               char ***strings, size_t *stringcount);


#endif

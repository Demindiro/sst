#ifndef LINKOBJ_H
#define LINKOBJ_H

#include <stddef.h>
#include "vasm.h"

void linkobj(const char **vbins, size_t *vbinlens, size_t vbincount,
             const struct lblmap *maps, char *output, size_t *_outputlen);

void obj_parse(const char *bin, size_t len, char *output, size_t *outputlen,
               struct lblmap *map);

#endif

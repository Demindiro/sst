#include "linkobj.h"
#include <string.h>
#include <stdlib.h>
#include <endian.h>
#include <stdint.h>
#include "hashtbl.h"
#include "vasm.h"
#include "util.h"


void linkobj(const char **vbins, size_t *vbinlens, size_t vbincount,
             const struct lblmap *maps, char *output, size_t *_outputlen)
{
	struct hashtbl lbl2pos;
	struct lblpos  pos2lbl[0x10000];
	output[0] = OP_JMP;
	pos2lbl[0].lbl = "_start";
	pos2lbl[0].pos = 1;
	size_t outputlen = 9;
	size_t pos2lblcount = 1;

	h_create(&lbl2pos, 32);

	for (size_t i = 0; i < vbincount; i++) {

		size_t len      = vbinlens[i];
		const char *ptr = vbins[i];

		DEBUG("  lbl2pos:");
		for (size_t j = 0; j < maps[i].lbl2poscount; j++) {
			const char *s = maps[i].lbl2pos[j].lbl;
			size_t pos    = maps[i].lbl2pos[j].pos;
			DEBUG("    %s = %lu (%lu + %lu)", s, pos + outputlen, outputlen, pos);
			if (h_add(&lbl2pos, s, pos + outputlen))
				EXITERRNO(3, "h_add");
		}

		DEBUG("  pos2lbl:");
		for (size_t j = 0; j < maps[i].pos2lblcount; j++) {
			const char *s = maps[i].pos2lbl[j].lbl;
			size_t pos    = maps[i].pos2lbl[j].pos;
			DEBUG("    %lu (%lu + %lu) = %s", pos + outputlen, outputlen, pos, s);
			pos2lbl[pos2lblcount].lbl = s;
			pos2lbl[pos2lblcount].pos = pos + outputlen;
			pos2lblcount++;
		}

		memcpy(output + outputlen, ptr, len);
		outputlen += len;
	}

	for (size_t i = 0; i < pos2lblcount; i++) {
		size_t pos = h_get(&lbl2pos, pos2lbl[i].lbl);
		*(size_t *)(output + pos2lbl[i].pos) = htobe64(pos);
		DEBUG("  %s @ %lu (0x%lx) --> %lu (0x%lx)", pos2lbl[i].lbl,
		       pos2lbl[i].pos, pos2lbl[i].pos, pos, pos);
	}

	*_outputlen = outputlen;
}


void obj_parse(const char *bin, size_t len, char *output, size_t *outputlen,
               struct lblmap *map)
{
	const char *ptr = bin + 4;

	DEBUG("  lbl2pos:");
	map->lbl2poscount = be32toh(*(uint32_t *)ptr);
	ptr += 4;
	for (size_t i = 0; i < map->lbl2poscount; i++) {
		uint8_t strl = *ptr;
		ptr += sizeof strl;
		char *s = malloc(strl + 1);
		if (s == NULL)
			EXITERRNO(3, "malloc");
		memcpy(s, ptr, strl);
		s[strl] = 0;
		ptr += strl;
		uint64_t pos = *(uint64_t *)ptr;
		pos = be64toh(pos);
		ptr += sizeof pos;
		DEBUG("    %s = %lu", s, pos);
		map->lbl2pos[i].lbl = s;
		map->lbl2pos[i].pos = pos;
	}

	DEBUG("  pos2lbl:");
	map->pos2lblcount = be32toh(*(uint32_t *)ptr);
	ptr += 4;
	for (size_t i = 0; i < map->pos2lblcount; i++) {
		uint8_t strl = *ptr;
		ptr += sizeof strl;
		char *s = malloc(strl + 1);
		if (s == NULL)
			EXITERRNO(3, "malloc");
		memcpy(s, ptr, strl);
		s[strl] = 0;
		ptr += strl;
		uint64_t pos = *(uint64_t *)ptr;
		pos = be64toh(pos);
		ptr += sizeof pos;
		DEBUG("    %lu = %s", pos, s);
		map->pos2lbl[i].lbl = s;
		map->pos2lbl[i].pos = pos;
	}

	len -= ptr - bin;
	DEBUG("%lu", len);
	memcpy(output, ptr, len);
	*outputlen = len;
}

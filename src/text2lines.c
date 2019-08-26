#include "text2lines.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "hashtbl.h"
#include "util.h"


int text2lines(const char *text,
               line_t **lines  , size_t *linecount,
               char ***strings, size_t *stringcount) {
	size_t ls     = 64, lc = 0;
	line_t *lns    = malloc(ls * sizeof *lns);
	size_t ss     = 4, sc = 0;
	char **strs   = malloc(ss * sizeof *strs);
	const char *c = text;

	while (1) {
		// Skip whitespace
		while (*c == '\n' || *c == ';' || *c == ' ' || *c == '\t')
			c++;
		// EOL
		if (*c == 0)
			break;
		// Comment
		if (*c == '#') {
			while (*c != '\n' && *c != 0)
				c++;
			continue;
		}
		// Copy line
		char buf[256], *ptr = buf;

		// Check if declaration + assignment
		const char *d = c;
		while (*c != ' ' && *c != '\t' && *c != '\n')
			c++;
		char b[64];
		size_t l = c - d;
		memcpy(b, d, l);
		b[l] = 0;
		if (streq(b, "long") || streq(b, "char[]")) {
			b[l] = ' ';
			l++;
			d = c + 1;
			while (*d == ' ' || *d == '\t')
				d++;
			c = d;
			while (*d != ' ' && *d != '\t' && *d != '\n') {
				b[l] = *d;
				l++, d++;
			}
			b[l] = 0;
			lns[lc].text = strclone(b);
			lns[lc].row  = 9999;
			lc++;
		} else {
			c = d;
		}
		while (*c != '\n' && *c != ';') {
			if (*c == ' ' || *c == '\t') {
				*ptr = ' ';
				ptr++;
				while (*c == ' ' || *c == '\t')
					c++;
				*ptr = *c;
			} else {
				*ptr = *c;
				ptr++, c++;
			}
			if (strncmp(c, "true", 4) == 0 && (c[4] == 0 || strchr("\n \t", c[4]))) {
				*ptr = '1';
				ptr += 1;
				c   += 4;
			} else if (strncmp(c, "false ", 5) == 0 && (c[5] == 0 || strchr("\n \t", c[5]))) {
				*ptr = '0';
				ptr += 1;
				c   += 5;
			} else if (*c == '"') {
				memcpy(ptr, ".str_", sizeof ".str_" - 1);
				ptr += sizeof ".str_" - 1;
				*ptr = '0' + sc;
				ptr++;
				char buf2[4096], *ptr2 = buf2;
				c++;
				while (*c != '"') {
					*ptr2 = *c;
					ptr2++, c++;
				}
				c++;
				char *p = malloc(ptr2 - buf2);
				memcpy(p, buf2, ptr2 - buf2 + 1);
				p[ptr2 - buf2] = 0;
				strs[sc] = p;
				sc++;
			}
			if (*c == '\'') {
				c++;
				int v = *c;
				c++;
				if (v == '\\') {
					c++;
					switch (*c) {
					case '0': v =  0; break;
					case 'n': v = 10; break;
					}
				}
				size_t l = sprintf(ptr, "%d", v);
				ptr += l;
				c++;
			}
		}
		char *m = malloc(ptr - buf + 1);
		memcpy(m, buf, ptr - buf);
		m[ptr - buf] = 0;
		lns[lc].text = m;
		lns[lc].row  = 9999;
		lc++;
		c++;
	}

	*linecount   = lc;
	*lines       = realloc(lns , lc * sizeof *lns);
	*stringcount = sc;
	*strings     = realloc(strs, sc * sizeof *strs);
	return 0;
}

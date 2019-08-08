#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "hashtbl.h"


char *strings[4096];
size_t stringcount;

char *lines[4096];
size_t linecount;


int text2lines(char *text) {
	char *c = text;
	while (1) {
		// Skip whitespace
		while (*c == '\n' || *c == ';' || *c == ' ' || *c == '\t')
			c++;
		// EOL
		if (*c == 0)
			break;
		// Copy line
		char buf[256], *ptr = buf;
		while (*c != '\n' && *c != ';') {
			*ptr = *c;
			ptr++, c++;
			if (*c == '"') {
				memcpy(ptr, ".str_", sizeof ".str_" - 1);
				ptr += sizeof ".str_" - 1;
				*ptr = '0' + stringcount;
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
				strings[stringcount] = p;
				stringcount++;
			}
			if (*c == '\t')
				*c = ' ';
		}
		char *m = malloc(ptr - buf + 1);
		memcpy(m, buf, ptr - buf);
		m[ptr - buf] = 0;
		lines[linecount] = m;
		linecount++;
		c++;
	}
}

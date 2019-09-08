#include "text2lines.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lines.h"
#include "util.h"


typedef struct pos2 {
	pos_t p;
	size_t c;
} pos2_t;


static int is_control_word(const char *c)
{
	const char *words[] = {
		"break", "end", "for", "if", "in", "include",
		"return", "to", "while", NULL
	};
	for (const char **w = words; *w != NULL; w++) {
		size_t l = strlen(*w);
		if (memcmp(c, *w, l) == 0 && (c[l] == ' ' || c[l] == '\n'))
			return 1;
	}
	return 0;
}


static int _hex2chr(const char *c)
{
	int y = 0;
	for (size_t i = 0; i < 2; i++) {
		int x = *c;
		if ('0' <= x && x <= '9')
			x -= '0';
		else if ('a' <= x && x <= 'f')
			x -= 'a';
		else if ('A' <= x && x <= 'F')
			x -= 'A';
		else
			EXIT(1, "Invalid character in hex number: '%c'", x);
		y <<= 4;
		y  |= x;
	}
	return y;
}


static int is_decl_and_assign(const char *c)
{
	// decl + assign format: type name = ...
	// Make sure the 'type' isn't a control word (e.g. 'if')
	if (is_control_word(c))
		return 0;
	// Skip type
	while (*c != '\t' && *c != ' ') {
		c++;
		if (*c == 0 || *c == '\n')
			return 0;
	}
	// Skip whitespace
	while (*c == '\t' || *c == ' ') {
		c++;
		if (*c == 0 || *c == '\n')
			return 0;
	}
	// Definitely not a declaration if there is already a '='
	if (*c == '=')
		return 0;
	// Skip name
	while (('a' <= *c && *c <= 'z') ||
	       ('A' <= *c && *c <= 'Z') ||
	       ('0' <= *c && *c <= '9') ||
	       (*c == '_')) {
		c++;
		if (*c == 0 || *c == '\n')
			return 0;
	}
	// Skip whitespace
	while (*c == '\t' || *c == ' ')
		c++;

	return *c == '=';
}


static char *_trim(const char *text, pos2_t **pos, size_t *poscount)
{
	size_t l = strlen(text) + 1;
	char *buf = malloc(l);
	memcpy(buf, text, l);
	char *d = buf, *s = buf;

	size_t pc = 0, pl = 1024;
	pos2_t *p = malloc(pl * sizeof *p);

	size_t c = 0;
	int x = 1, y = 1;

	int instring = 0;

#define INCS do {		\
	s++, x++;		\
	if (*s == '\n')		\
		y++, x = 0;	\
} while (0)
#define INCD do {		\
	d++, c++;		\
} while (0)
#define MARK do {		\
	p[pc].p.c = s - buf;	\
	p[pc].p.x = x;		\
	p[pc].p.y = y;		\
	p[pc].c   = c;		\
	pc++;			\
} while(0)

	while (1) {
		assert(x >= 0);
		assert(y >  0);
		// Skip whitespace
		while (*s == ' ' || *s == '\t' || *s == '\n')
			INCS;
		// EOL
		if (*s == 0)
			break;
		// ';' has the same meaning as '\n'
		if (*s == ';') {
			*s = '\n';
			s++, x++;
			continue;
		}
		// Insert position marker
		MARK;
		// Copy non-whitespace
		while (instring || (*s != ' ' && *s != '\t' && *s != '\n')) {
			// Include strings fully
			if (*s == '"')
				instring = !instring;
			*d = *s;
			INCD;
			INCS;
			if (*s == 0)
				break;
		}
		// Insert ' ' or '\n'
		*d = *s == '\n' ? '\n' : ' ';
		INCD;
	}

#undef INCS
#undef INCD
#undef MARK

	d[-1] = '\n'; // Make sure the last line ends with a newline
	d[ 0] = 0; // Make sure the text ends with a null terminator

	*pos = realloc(p, pc * sizeof *p);
	*poscount = pc;

	return realloc(buf, d - buf + 1);
}


static pos2_t _getpos(pos2_t *p, size_t pc, size_t c)
{
	for (size_t i = pc; i > 0; i--) {
		pos2_t n = p[i - 1];
		if (n.c <= c) {
		       	n.p.x += c - n.c;
			//n.p.c  = c;
			return n;
		}
	}
	assert(0);
}


static int can_extend_shortmath(const char *c)
{
	while (*c != ' ') {
		c++;
		if (*c == '\n')
			return 0;
	}
	c++;
	return strchr("+-*/%<>&|^", *c) && (*(c + 1) == '=');
}


static void _substitute_char_with_num(const char **_c, char **_ptr)
{
	const char *c = *_c;
	char *ptr = *_ptr;
	if (*c == '\'') {
		c++;
		int v = *c;
		c++;
		if (v == '\\') {
			switch (*c) {
			case '0' : v = '\0'; break;
			case 'a' : v = '\a'; break;
			case 'b' : v = '\b'; break;
			case 'f' : v = '\f'; break;
			case 'n' : v = '\n'; break;
			case 'r' : v = '\r'; break;
			case 't' : v = '\t'; break;
			case 'v' : v = '\v'; break;
			case '\'': v = '\''; break;
			case 'x':
				c++;
				v = _hex2chr(c);
				c++;
				break;
			default:
				WARN("Invalid escape: '\\%c'", *c);
			}
			c++;
		}
		size_t l = sprintf(ptr, "%d", v);
		ptr += l;
		c++;
	}
	*_c   = c;
	*_ptr = ptr;
}


int text2lines(const char *text,
               line_t **lines , size_t *linecount,
               char ***strings, size_t *stringcount) {

	pos2_t *pos;
	size_t poscount;
	text = _trim(text, &pos, &poscount);

	size_t ls     = 1024, lc = 0;
	line_t *lns    = malloc(ls * sizeof *lns);
	size_t ss     = 64, sc = 0;
	char **strs   = malloc(ss * sizeof *strs);
	const char *c = text;

	while (*c != 0) {

		// Skip comments
		if (*c == '#') {
			while (*c != '\n')
				c++;
			c++;
			continue;
		}

		// Split declaration + assignment
		if (is_decl_and_assign(c)) {
			char        b[256];
			size_t      l = 0;
			const char *s = c;
			// Include type
			while (*c != ' ')
				b[l++] = *c++;
			b[l++] = ' ';
			// Skip whitespace
			while (*c == ' ')
				c++;
			// Include name (don't use c because the name is needed later)
			const char *d = c;
			while (!strchr(" \t=+-*/%<>", *d)) {
				b[l++] = *d;
				d++;
			}
			pos2_t p = _getpos(pos, poscount, s - text);
			b[l] = 0;
			lns[lc].text = strclone(b);
			lns[lc].pos  = p.p;
			lc++;
		}

#if 0
		// Extend var++ and var-- if applicable
		if (can_extend_inc_or_dec(c)) {
			const char *d = c;
			while (*c != '-' && *c != '+')
				c++;
			char buf0[256], buf1[1024];
			memcpy(buf0, d, c - d);
			buf0[c - d] = 0;
			snprintf(buf1, sizeof buf1, "%s = %s %c 1", buf0, buf0, *c);
			pos2_t p = _getpos(pos, poscount, *c);
			lns[lc].text = strclone(buf1);
			lns[lc].pos  = p.p;
			lc++;
			c += 3;
			continue;
		}
#endif

		// Extend +=, -= ... if applicable
		if (can_extend_shortmath(c)) {

			const char *d = c;
			while (*c != ' ')
				c++;
			char *buf0 = strnclone(d, c - d);

			c++;
			char op = *c;
			c += 3; // op + '=' + ' '

			d  = c;
			while (*c != '\n')
				c++;
			char *buf1 = strnclone(d, c - d);
			char *e = buf1;
			do {
				_substitute_char_with_num((const char **)&e, &e);
			} while (*e++ != 0);
			// TODO: braces
			pos2_t p = _getpos(pos, poscount, *c);
			lns[lc].text = strprintf("%s = %s %c (%s)", buf0, buf0, op, buf1);
			lns[lc].pos  = p.p;
			free(buf0);
			free(buf1);
			lc++;
			c++;
			continue;
		}

		// Copy line
		char buf[256], *ptr = buf;
		const char *d = c;

		while (*c != '\n' && *c != '#') {
			*ptr++ = *c++;
			if (*c == '=' && (*(c + 2) == '+' || *(c + 2) == '-')) {
				*ptr++ = '=';
				*ptr++ = ' ';
				*ptr++ = '0';
				c++;
			} else if (strncmp(c, "true", 4) == 0 && (c[4] == 0 || strchr("\n ", c[4]))) {
				*ptr = '1';
				ptr += 1;
				c   += 4;
			} else if (strncmp(c, "false ", 5) == 0 && (c[5] == 0 || strchr("\n ", c[5]))) {
				*ptr = '0';
				ptr += 1;
				c   += 5;
			} else if (strchr("+-*/%<>", *(c-1))) {
				if (strchr(" =+-*/%<>\n", *c))
					continue;
				*ptr++ = ' ';
			} else if (*c == '"') {
				memcpy(ptr, "_str_", sizeof ".str_" - 1);
				ptr += sizeof ".str_" - 1;
				*ptr = '0' + sc;
				ptr++;
				char buf2[4096], *ptr2 = buf2;
				c++;
				while (*c != '"') {
					*ptr2 = *c;
					ptr2++;
					c++;
				}
				c++;
				char *p = malloc(ptr2 - buf2 + 1);
				memcpy(p, buf2, ptr2 - buf2);
				p[ptr2 - buf2] = 0;
				strs[sc] = p;
				sc++;
			} else if (*c == ',' && *(c + 1) == ' ') {
				*ptr++ = ',';
				c += 2;
			} else if (*(c - 1) == ',' && *c == ' ') {
				c++;
			} else if (*c == '\'') {
				c++;
				int v = *c;
				c++;
				if (v == '\\') {
					switch (*c) {
					case '0' : v = '\0'; break;
					case 'a' : v = '\a'; break;
					case 'b' : v = '\b'; break;
					case 'f' : v = '\f'; break;
					case 'n' : v = '\n'; break;
					case 'r' : v = '\r'; break;
					case 't' : v = '\t'; break;
					case 'v' : v = '\v'; break;
					case '\'': v = '\''; break;
					case 'x':
						c++;
						v = _hex2chr(c);
						c++;
						break;
					default:
						WARN("Invalid escape: '\\%c'", *c);
						break;
					}
					c++;
				}
				size_t l = sprintf(ptr, "%d", v);
				ptr += l;
				c++;
			}
		}
		*ptr = 0;
		const char *m = strclone(buf);
		pos2_t p = _getpos(pos, poscount, d - text);
		lns[lc].text = m;
		lns[lc].pos  = p.p;
		lc++;
		// Don't increment so that the next iteration detects the comment
		if (*c != '#')
			c++;
	}

	// Post processing
	for (size_t i = 0; i < lc; i++) {
		const char *t = lns[i].text;
		// Split 'elif' into 'else' and 'if' and append 'end'
		if (strstart(t, "elif ")) {
			const char *s = t;
			memmove(lns + i + 1, lns + i, (lc - i) * sizeof *lns);
			lc++;
			lns[i++].text = "else";
			lns[i++].text = strclone(s + strlen("el"));
			size_t j = i;
			while (!streq(lns[j].text, "end"))
				j++;
			if (streq(lns[j].text, "else"))
				continue;
			memmove(lns + j + 1, lns + j, (lc - j) * sizeof *lns);
			lc++;
			lns[j++].text = "end";
		}
		// Convert 'x++' to 'x = x + 1'
		else if (strend(t, "++") || strend(t, "--")) {
			size_t l = strlen(t) - 2;
			if (l == 0)
				EXIT(1, "%s must be preceded by a variable name", t);
			lns[i].text = strprintf("%.*s = %.*s %c 1", l, t, l, t, t[l]);
		}
	}

	*linecount   = lc;
	*lines       = realloc(lns , lc * sizeof *lns);
	*stringcount = sc;
	*strings     = realloc(strs, sc * sizeof *strs);
	return 0;
}

#include <stdio.h>
#include <string.h>
#include "expr.h"
#include "func.h"
#include "hashtbl.h"
#include "util.h"
#include "var.h"



static int _op_precedence(char c0, char c1)
{
	switch (c0) {
	case '*':
		if (c0 == c1) // **
			return 8;
	case '%':
		if (c0 == c1) // %%
			return 7;
	case '/':
		if (c1 != 0)
			goto _default;
		return 7;
	case '+':
	case '-':
		if (c1 != 0)
			goto _default;
		return 6;
	case '|':
	case '&':
		if (c0 == c1) // Boolean comparison
			return 4;
	case '^':
		if (c1 != 0)
			goto _default;
		return 5;
	case '=':
	case '!':
		if (c1 == '=') // Comparison
			return 3;
	case '<':
	case '>':
		return 3;
	default:
	_default:
		DEBUG("%d", c0);
		EXIT(1, "Invalid math operator: '%s'", &c0); // *pukes*
	}
}


static int _str2op(const char *op)
{
	if (streq(op, "=="))
		return -MATH_SUB;
	else if (streq(op, "!="))
		return MATH_SUB;
	else if (streq(op, "*"))
		return MATH_MUL;
	else if (streq(op, "/"))
		return MATH_DIV;
	else if (streq(op, "%"))
		return MATH_REM;
	else if (streq(op, "%%"))
		return MATH_MOD;
	else if (streq(op, "-"))
		return MATH_SUB;
	else if (streq(op, "+"))
		return MATH_ADD;
	else if (streq(op, "**"))
		EXIT(4, "Not implemented");
	else if (streq(op, "&"))
		return MATH_AND;
	else if (streq(op, "|"))
		return MATH_OR;
	else if (streq(op, "^"))
		return MATH_XOR;
	else if (streq(op, "||"))
		return MATH_L_OR;
	else if (streq(op, "&&"))
		return MATH_L_AND;
	else if (streq(op, "<"))
		return MATH_LESS;
	else if (streq(op, ">"))
		return -MATH_LESS;
	else if (streq(op, "<="))
		return MATH_LESSE;
	else if (streq(op, ">="))
		return -MATH_LESSE;
	else if (streq(op, "<<"))
		return MATH_LSHIFT;
	else if (streq(op, ">>"))
		return MATH_RSHIFT;
	EXIT(1, "Unknown OP '%s'", op);
}


const char *parse_expr(func f, const char *str, char *istemp, const char *type,
                       hashtbl vartypes, hashtbl functbl)
{
	static size_t tempvarcounter = 0;

	// The legacy cruft lives on
	// TODO
	if (strstart(str, "new ")) {
		// Forgive me for I am lazy
		const char *v = new_temp_var(f, strclone(str + 4), "new");
		const char *l = strnclone(strchr(str, '[') + 1, 1); // *puke*
		const char *a[1] = { l };
		line_function(f, v, "alloc", 1, a);
		*istemp = 1;
		return v;
	}

	// Check if it is a function call
	{
		const char *c = str;
		while (*c != ' ' && *c != 0)
			c++;
		char b[256];
		memcpy(b, str, c - str);
		b[c - str] = 0;
		func g;
		if (h_get2(functbl, b, (size_t *)&g) != -1) {
			const char *x = strprintf("__e%lu", tempvarcounter++);
			line_declare(f, g->type, x);
			line_function_parse(f, x, str, functbl, vartypes);
			*istemp = 1;
			return x;
		}
	}

	// If there are no spaces, it's a single variable
	for (const char *c = str; *c != 0; c++) {
		if (*c == ' ')
			goto notavar;
	}
	*istemp = 0;
	return strclone(deref_var(str, f, vartypes, istemp));

notavar:;
	// Put braces in accordance to order of precedence
	char buf0[1024], buf1[1024];
	char *sp = buf0, *sb = buf1;
	size_t n = strlen(strcpy(sp, str));
	sp[n + 0] = ')';
	sp[n + 1] =  0;

	char *s = sp;
	while (*s != 0) {
		// Get vars and ops
		char vars[3][64] = {};
		char ops[2][3] = {};
		size_t tlvl = 1;
		for (size_t i = 0; i < 3; i++) {
			size_t lvl = 1;
			char *p = vars[i];
			while (*s != ' ' || lvl > 1) {
				if (*s == '(')
					lvl++, tlvl++;
				if (*s == ')')
					lvl--, tlvl--;
				if (tlvl == 0)
					goto done;
				if (lvl == 0)
					break;
				*p++ = *s++;
			}
			*p = 0;
			s++;
			if (i < 2) {
				ops[i][0] = *s++;
				ops[i][1] = *s != ' ' ? *s++ : 0;
				s++;
			}
		}
done:
		// Precedence doesn't apply on expressions with only one op
		if (ops[1][0] == 0) {
			s++;
			if (*s == 0)
				break;
			s++;
			continue;
		}
		// Get precedences
		int precdl = _op_precedence(ops[0][0], ops[0][1]);
		int precdr = _op_precedence(ops[1][0], ops[1][1]);
		// Left-to-right is default in case of equal precedence
		if (precdl >= precdr) {
			// Put braces around the first and second var (with
			// the op), tack the other op and var on and copy
			// the remainder.
			size_t n = sprintf(sb, "(%s %s %s) %s %s %s",
				 	vars[0], ops[0], vars[1],
				 	ops[1], vars[2], s);
			if (streq(s, ")")) {
				sb[n - 2] = ')';
				sb[n - 1] =  0;
			}
			// Swap the buffers
			SWAP(char *, sp, sb);
			s = sp;
		} else {
			// Ditto, pretty much
			size_t n = sprintf(sb, "%s %s (%s %s %s) %s",
					vars[0], ops[0], vars[1],
					ops[1], vars[2], s);
			if (streq(s, ")")) {
				sb[n - 2] = ')';
				sb[n - 1] =  0;
			}
			SWAP(char *, sp, sb);
			s = sp;
		}
	}

	// The expression is now effectively 'reduced' to two 'variables'
	// and one operator
	char vl[1024], vr[1024];
	char op[3] = {};
	const char *p = sp;
	// Copy the left variable
	char *v = vl;
	size_t lvl = 1;
	while (*p != ' ' || lvl > 1) {
		if (*p == '(')
			lvl++;
		if (*p == ')')
			lvl--;
		if (lvl == 0)
			break;
		*v++ = *p++;
	}
	*v = 0;
	p++;
	// Copy the operator
	op[0] = *p++;
	op[1] = *p != ' ' ? *p++ : 0;
	p++;
	// Copy the right variable
	v = vr;
	lvl = 1;
	while (*p != ' ' || lvl > 1) {
		if (*p == '(')
			lvl++;
		if (*p == ')')
			lvl--;
		if (lvl == 0)
			break;
		*v++ = *p++;
	}
	*v = 0;
	// Remove redundant braces
	n = strlen(vl);
	lvl = 0;
	for (size_t i = 0; i < n; i++) {
		if (vl[i] == '(')
			lvl++;
		if (vl[i] == ')')
			lvl--;
		if (lvl == 0) {
			if (n >= 2 && i == n - 1) {
				n -= 2;
				memmove(vl, vl + 1, n);
				vl[n] = 0, i = 0;
			} else {
				break;
			}
		}
	}
	n = strlen(vr);
	lvl = 0;
	for (size_t i = 0; i < n; i++) {
		if (vr[i] == '(')
			lvl++;
		if (vr[i] == ')')
			lvl--;
		if (lvl == 0) {
			if (n >= 2 && i == n - 1) {
				n -= 2;
				memmove(vr, vr + 1, n);
				vr[n] = 0, i = 0;
			} else {
				break;
			}
		}
	}

	// Parse the 'variables' as expressions
	char ity, itz;
	const char *y = parse_expr(f, vl, &ity, type, vartypes, functbl);
	const char *z = parse_expr(f, vr, &itz, type, vartypes, functbl);

	// Create a temporary variable
	const char *x = strprintf("__e%lu", tempvarcounter++);

	// Declare it
	line_declare(f, x, type);

	// Get the op and swap vars if necessary
	int o = _str2op(op);
	int invif = 0;
	if (o < 0) {
		o = -o;
		// Swapping won't help for ==. Inverting does
		if (o == MATH_SUB) {
			invif = 1;
		} else {
			SWAP(const char *, y, z);
			SWAP(char, ity, itz);
		}
	}

	// Add the math expression
	line_math(f, o, x, y, z);
	if (invif)
		line_math(f, MATH_INV, x, x, NULL);
	char _[256];
	line2str(f->lines[f->linecount - 1], _, sizeof _);

	// Destroy the temporary variables
	if (ity) line_destroy(f, y);
	if (itz) line_destroy(f, z);

	// Yay
	*istemp = 1;
	return x;
}

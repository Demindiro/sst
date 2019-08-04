/**
 * Steps:
 *   0. Parse text into lines
 *   1. Parse lines into structs
 *   2. Optimize structs
 *   3. Parse funcs into virt assembly
 *   4. Optimize virt assembly
 *   5. Convert virt assembly to real assembly
 *   6. Optimize real assembly
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vasm.h"
#include "hashtbl.h"


#define FUNC_LINE_NONE   0
#define FUNC_LINE_ASSIGN 1
#define FUNC_LINE_FUNC   2
#define FUNC_LINE_GOTO   3
#define FUNC_LINE_IF     4
#define FUNC_LINE_LABEL  5
#define FUNC_LINE_MATH   6
#define FUNC_LINE_RETURN 7

#define MATH_ADD VASM_OP_ADD
#define MATH_SUB VASM_OP_SUB
#define MATH_MUL VASM_OP_MUL
#define MATH_DIV VASM_OP_DIV


//#define streq(x,y) (strcmp(x,y) == 0)
#define isnum(c) ('0' <= c && c <= '9')


struct variable {
	char type[32];
	char name[32];
};

struct func_line {
	char type;
};

struct func_line_assign {
	struct func_line line;
	char *var;
	char *value;
};

struct func_line_func {
	struct func_line line;
	unsigned char paramcount;
	struct variable  assign;
	char name[32];
	char params[32][32];
};

struct func_line_goto {
	struct func_line line;
	char *label;
};

struct func_line_if {
	struct func_line line;
	char *label;
	char *x, *y;
};

struct func_line_label {
	struct func_line line;
	char *label;
};

struct func_line_math {
	struct func_line line;
	char op;
	char *x, *y, *z;
};

struct func_line_return {
	struct func_line line;
	char *val;
};


struct func_arg {
	char type[32];
	char name[32];
};


struct func {
	char type[32];
	char name[32];
	unsigned char argcount;
	unsigned char linecount;
	struct func_arg   args[16];
	struct func_line *lines[256];
};


char *strings[4096];
size_t stringcount;

char *lines[4096];
size_t linecount;

struct func funcs[4096];
size_t funccount;

struct vasm vasms[4096];
size_t vasmcount;

char vbin[0x10000];
size_t vbinlen;




static char *strclone(const char *text) {
	size_t l = strlen(text);
	char *m = malloc(l + 1);
	if (m == NULL)
		return m;
	memcpy(m, text, l + 1);
	return m;
}



static int text2lines(char *text) {
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
				memcpy(ptr, "__str_", sizeof "__str_" - 1);
				ptr += sizeof "__str_" - 1;
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


static int parsefunc(size_t start, size_t end) {

	size_t i = 0, j = 0;
	char *line = lines[start];
	struct func *f = &funcs[funccount];
	
	// Parse declaration
	// Get type
	while (line[j] != ' ')
		j++;
	memcpy(f->type, line + i, j - i);

	// Skip whitespace
	while (line[j] == ' ')
		j++;
	i = j;

	// Get name
	while (line[j] != ' ' && line[j] != '(')
		j++;
	memcpy(f->name, line + i, j - i);

	// Skip whitespace
	while (line[j] == ' ')
		j++;
	i = j;

	// Parse arguments
	if (line[j] != '(')
		abort();
	j++;
	size_t k = 0;
	for ( ; ; ) {
		// Argument list done
		if (line[j] == ')')
			break;
		// Skip whitespace
		while (line[j] == ' ')
			j++;
		i = j;
		// Get type
		while (line[j] != ' ')
			j++;
		memcpy(f->args[k].type, line + i, j - i);
		// Skip whitespace
		while (line[j] == ' ')
			j++;
		i = j;
		// Get type
		while (line[j] != ' ' && line[j] != ')')
			j++;
		memcpy(f->args[k].name, line + i, j - i);
		// Skip whitespace
		while (line[j] == ' ')
			j++;
	}
	f->argcount = k;

	// Parse lines
	start++;
	line = lines[start];
	k = 0;
	struct func_line_goto  *forjmps[16];
	struct func_line_math  *formath[16];
	struct func_line_label *forends[16];
	int forcounter = 0;
	int forcount   = 0;
	for ( ; ; ) {
		// Parse first 16 words for analysis
		char word[16][32];
		memset(word, 0, sizeof word);
		i = j = 0;
		for (size_t l = 0; l < sizeof word / sizeof *word; l++) {
			// Skip whitespace
			while (line[j] == ' ')
				j++;
			i = j;
			// Get word
			char in_quotes = 0;
			while (in_quotes || (line[j] != ' ' && line[j] != 0)) {
				if (line[j] == '"')
					in_quotes = !in_quotes;
				j++;
			}
			memcpy(word[l], line + i, j - i);
		}
		// Determine line type
		if (streq(word[0], "end")) {
			if (forcount > 0) {
				forcount--;
				f->lines[k] = (void *)formath[forcount];
				k++;
				f->lines[k] = (void *)forjmps[forcount];
				k++;
				f->lines[k] = (void *)forends[forcount];
				k++;
			} else {
				break;
			}
		} else if (streq(word[0], "for")) {
			if (!streq(word[2], "in")) {
				fprintf(stderr, "Expected 'in', got '%s'\n", word[2]);
				return -1;
			}
			if (!streq(word[4], "to")) {
				fprintf(stderr, "Expected 'to', got '%s'\n", word[4]);
				return -1;
			}

			struct func_line_assign *fla = calloc(sizeof *fla, 1);
			fla->line.type = FUNC_LINE_ASSIGN;
			fla->var       = strclone(word[1]);
			fla->value     = strclone(word[3]);
			f->lines[k]    = (struct func_line *)fla;
			k++;

			char lbl[16];
		       	snprintf(lbl, sizeof lbl, "__for_%d", forcounter);

			struct func_line_label  *fll = calloc(sizeof *fll, 1);
			fll->line.type = FUNC_LINE_LABEL;
			fll->label     = strclone(lbl);
			f->lines[k]    = (struct func_line *)fll;
			k++;

			formath[forcount] = calloc(sizeof *formath[forcount], 1);
			formath[forcount]->line.type = FUNC_LINE_MATH;
			formath[forcount]->op       = MATH_ADD;
			formath[forcount]->x        = fla->var;
			formath[forcount]->y        = fla->var;
			formath[forcount]->z        = "1";

			forjmps[forcount] = calloc(sizeof *forjmps[forcount], 1);
			forjmps[forcount]->line.type = FUNC_LINE_GOTO;
			forjmps[forcount]->label     = fll->label;

		       	snprintf(lbl, sizeof lbl, "__for_%d_end", forcounter);
			forends[forcount] = calloc(sizeof *forends[forcount], 1);
			forends[forcount]->line.type = FUNC_LINE_LABEL;
			forends[forcount]->label     = strclone(lbl);

			struct func_line_if     *fli = calloc(sizeof *fli, 1);
			fli->line.type = FUNC_LINE_IF;
			fli->label     = forends[forcount]->label;
			fli->x         = fla->var;
			fli->y         = strclone(word[5]);
			f->lines[k]    = (struct func_line *)fli;
			k++;

			forcount++;
			forcounter++;
		} else if (streq(word[0], "return")) {
			struct func_line_return *flr = calloc(sizeof *flr, 1);
			flr->line.type = FUNC_LINE_RETURN;
			flr->val = strclone(word[1]);
			f->lines[k] = (struct func_line *)flr;
			k++;
		} else {
			struct func_line_func *flf = calloc(sizeof *flf, 1);
			strcpy(flf->name, word[0]);
			if (word[1][0] != 0) {
				strcpy(flf->params[0], word[1]);
				flf->paramcount++;
				if (word[2][0] != 0) {
					strcpy(flf->params[1], word[2]);
					flf->paramcount++;
					// TODO other words
				}
			}
			flf->line.type = FUNC_LINE_FUNC;
			f->lines[k] = (struct func_line *)flf;
			k++;
		}

		// NEXT PLEASE!!!
		start++;
		line = lines[start];
	}
	f->linecount = k;

	return 0;
}


static int lines2structs() {

	for (size_t i = 0; i < linecount; i++) {
		char *line = lines[i];
		size_t l = strlen(line);
		if (line[l - 1] == ')') {
			size_t start = i;
			// Find end of func
			while (strcmp(lines[i], "end") != 0)
				i++;
			parsefunc(start, i);
			funccount++;
		}
	}

	return 0;
}



static int func2vasm(struct func *f) {
	union vasm_all a;
	a.s.op  = VASM_OP_LABEL;
	a.s.str = f->name;
	vasms[vasmcount] = a.a;
	vasmcount++;

	struct hashtbl tbl;
	if (h_create(&tbl, 8) < 0) {
		perror("Failed to create hash table");
		return -1;
	}

	char allocated_regs[32];
	memset(allocated_regs, 0, sizeof allocated_regs);

	for (size_t i = 0; i < f->linecount; i++) {
		struct func_line_assign *fla;
		struct func_line_func   *flf;
		struct func_line_goto   *flg;
		struct func_line_if     *fli;
		struct func_line_label  *fll;
		struct func_line_math   *flm;
		struct func_line_return *flr;
		char ra, rb;
		switch (f->lines[i]->type) {
		case FUNC_LINE_ASSIGN:
			fla = (struct func_line_assign *)f->lines[i];
			if ('0' <= *fla->var && *fla->var <= '9') {
				fprintf(stderr, "You can't assign to a number\n");
				abort();
			}
			size_t reg = 0;
			for ( ; reg < sizeof allocated_regs / sizeof *allocated_regs; reg++) {
				if (!allocated_regs[reg]) {
					allocated_regs[reg] = 1;
					break;
				}
			}
			if (h_add(&tbl, fla->var, reg) < 0) {
				fprintf(stderr, "Failed to add variable to hashtable\n");
				abort();
			}
			if ('0' <= *fla->value && *fla->value <= '9') {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = reg;
				a.rs.str = fla->value;
			} else {
				a.r2.op   = VASM_OP_MOV;
				a.r2.r[0] = reg;
				reg = h_get(&tbl, fla->value);
				if (reg == -1) {
					fprintf(stderr, "Variable not defined\n");
					abort();
				}
				a.r2.r[1] = reg;
			}
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_FUNC:
			flf = (struct func_line_func *)f->lines[i];
			size_t j = 0;
			for ( ; j < 16 && j < flf->paramcount; j++) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = j;
				a.rs.str = flf->params[j];
				vasms[vasmcount] = a.a;
				vasmcount++;
			}
			for ( ; j < flf->paramcount; j++) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = 16;
				a.rs.str = flf->params[j];
				vasms[vasmcount] = a.a;
				vasmcount++;
				a.r.op = VASM_OP_PUSH;
				a.r.r  = 16;
				vasms[vasmcount] = a.a;
				vasmcount++;
			}
			a.s.op  = VASM_OP_CALL;
			a.s.str = flf->name;
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_GOTO:
			flg = (struct func_line_goto *)f->lines[i];
			a.s.op  = VASM_OP_JMP;
			a.s.str = flg->label;
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_IF:
			fli = (struct func_line_if *)f->lines[i];
			if (isnum(*fli->x)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = fli->x;
				vasms[vasmcount] = a.a;
				vasmcount++;
			} else {
				ra = h_get(&tbl, fli->x);
				if (ra == -1) {
					fprintf(stderr, "RIPOERZ\n");
					abort();
				}
			}
			if (isnum(*fli->y)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = rb = 21;
				a.rs.str = fli->y;
				vasms[vasmcount] = a.a;
				vasmcount++;
			} else {
				rb = h_get(&tbl, fli->y);
				if (rb == -1) {
					fprintf(stderr, "EOPFJEOPF\n");
					abort();
				}
			}
			a.r2s.op   = VASM_OP_JE;
			a.r2s.r[0] = ra;
			a.r2s.r[1] = rb;
			a.r2s.str  = fli->label;
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_LABEL:
			fll = (struct func_line_label *)f->lines[i];
			a.s.op  = VASM_OP_LABEL;
			a.s.str = fll->label;
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_MATH:
			flm = (struct func_line_math *)f->lines[i];
			if (isnum(*flm->x)) {
				fprintf(stderr, "You can't assign to a number\n");
				abort();
			}
			if (isnum(*flm->y)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = ra = 20;
				a.rs.str = flm->y;
				vasms[vasmcount] = a.a;
				vasmcount++;
			} else {
				ra = h_get(&tbl, flm->y);
				if (ra == -1) {
					fprintf(stderr, "RIPOfzkzefopozezERZ\n");
					abort();
				}
			}
			if (isnum(*flm->z)) {
				a.rs.op  = VASM_OP_SET;
				a.rs.r   = rb = 21;
				a.rs.str = flm->z;
				vasms[vasmcount] = a.a;
				vasmcount++;
			} else {
				rb = h_get(&tbl, flm->z);
				if (rb == -1) {
					fprintf(stderr, "RIPOERefnzfezfzeZ\n");
					abort();
				}
			}
			a.r3.op   = flm->op;
			a.r3.r[0] = h_get(&tbl, flm->x);
			a.r3.r[1] = ra;
			a.r3.r[2] = rb;
			if (a.r3.r[0] == -1) {
				fprintf(stderr, "zeifjizefjRIPOERefnzfezfzeZ\n");
				abort();
			}
			vasms[vasmcount] = a.a;
			vasmcount++;
			break;
		case FUNC_LINE_RETURN:
			flr = (struct func_line_return *)f->lines[i];
			a.rs.op  = VASM_OP_SET;
			a.rs.r   = 0;
			a.rs.str = flr->val;
			vasms[vasmcount] = a.a;
			vasmcount++;
			vasms[vasmcount].op = VASM_OP_RET;
			vasmcount++;
			break;
		default:
			fprintf(stderr, "Unknown line type (%d)\n", f->lines[i]->type);
			abort();
		}
	}
	a.a.op  = VASM_OP_RET;
	vasms[vasmcount] = a.a;
	vasmcount++;
	return 0;
}


static int structs2vasm() {
	for (size_t i = 0; i < funccount; i++)
		func2vasm(&funcs[i]);
}


int main(int argc, char **argv) {
	
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <input> <output>", argv[0]);
		return 1;
	}

	// Read source
	char buf[0x10000];
	int fd = open(argv[1], O_RDONLY);
	read(fd, buf, sizeof buf);
	close(fd);

	// 0. Text to lines
	printf("=== text2lines ===\n");
	text2lines(buf);
	printf("\n");
	for (size_t i = 0; i < stringcount; i++)
		printf("__str_%i = \"%s\"\n", i, strings[i]);
	printf("\n");
	for (size_t i = 0; i < linecount; i++)
		printf("%4d | %s\n", i + 1, lines[i]);
	printf("\n");

	// 1. Lines to structs
	printf("=== lines2structs ===\n");
	lines2structs();
	printf("\n");
	for (size_t i = 0; i < funccount; i++) {
		struct func *f = &funcs[i];
		printf("Name:   %s\n", f->name);
		printf("Return: %s\n", f->type);
		printf("Args:   %d\n", f->argcount);
		for (size_t j = 0; j < f->argcount; j++)
			printf("  %s -> %s\n", f->args[j].name, f->args[j].type);
		printf("Lines:  %d\n", f->linecount);
		for (size_t j = 0; j < f->linecount; j++) {
			struct func_line_assign *fla;
			struct func_line_func   *flf;
			struct func_line_goto   *flg;
			struct func_line_if     *fli;
			struct func_line_label  *fll;
			struct func_line_math   *flm;
			struct func_line_return *flr;
			switch (f->lines[j]->type) {
			case FUNC_LINE_ASSIGN:
				fla = (struct func_line_assign *)f->lines[j];
				printf("  Assign: %s = %s\n", fla->var, fla->value);
				break;
			case FUNC_LINE_FUNC:
				flf = (struct func_line_func *)f->lines[j];
				printf("  Function:");
				if (flf->assign.name[0] != 0)
					printf(" %s %s =", flf->assign.name, flf->assign.type);
				printf(" %s", flf->name);
				if (flf->paramcount > 0)
					printf(" %s", flf->params[0]);
				for (size_t k = 1; k < flf->paramcount; k++)
					printf(",%s", flf->params[k]);
				printf("\n");
				break;
			case FUNC_LINE_GOTO:
				flg = (struct func_line_goto *)f->lines[j];
				printf("  Goto: %s\n", flg->label);
				break;
			case FUNC_LINE_IF:
				fli = (struct func_line_if *)f->lines[j];
				printf("  If %s == %s then %s\n", fli->x, fli->y, fli->label);
				break;
			case FUNC_LINE_LABEL:
				fll = (struct func_line_label *)f->lines[j];
				printf("  Label: %s\n", fll->label);
				break;
			case FUNC_LINE_MATH:
				flm = (struct func_line_math *)f->lines[j];
				printf("  Math: %s = %s %s %s\n", flm->x, flm->y, "add", flm->z);
				break;
			case FUNC_LINE_RETURN:
				flr = (struct func_line_return *)f->lines[j];
				printf("  Return: %s\n", flr->val);
				break;
			default:
				printf("  Unknown line type (%d)\n", f->lines[j]->type);
				abort();
			}
		}
	}
	printf("\n");

	// 3. Structs to virt assembly
	fd = open(argv[2], O_WRONLY | O_CREAT);
	write(fd, vbin, vbinlen);
	close(fd);
	printf("=== structs2vasm ===\n");
	structs2vasm();
	printf("\n");
	FILE *_f = fopen(argv[2], "w");
	#define teeprintf(...) do {             \
		fprintf(stderr, ##__VA_ARGS__); \
		fprintf(_f, ##__VA_ARGS__);     \
	} while (0)
	for (size_t i = 0; i < vasmcount; i++) {
		union vasm_all a;
		a.a = vasms[i];
		switch (a.a.op) {
		default:
			fprintf(stderr, "Unknown OP (%d)\n", vasms[i].op);
			abort();
		case VASM_OP_NONE:
			printf("\n");
			break;
		case VASM_OP_COMMENT:
			for (size_t j = 0; j < vasmcount; j++) {
				if (vasms[j].op == VASM_OP_NONE || vasms[j].op == VASM_OP_COMMENT)
					continue;
				if (vasms[j].op == VASM_OP_LABEL)
					teeprintf("# %s\n", a.s.str);
				else
					teeprintf("\t# %s\n", a.s.str);
			}
			break;
		case VASM_OP_LABEL:
			teeprintf("%s:\n", a.s.str);
			break;
		case VASM_OP_NOP:
			teeprintf("\tnop\n");
			break;
		case VASM_OP_CALL:
			teeprintf("\tcall\t%s\n", a.s.str);
			break;
		case VASM_OP_RET:
			teeprintf("\tret\n");
			break;
		case VASM_OP_JMP:
			teeprintf("\tjmp\t%s\n", a.s.str);
			break;
		case VASM_OP_JE:
			teeprintf("\tje\t%s,r%d,r%d\n", a.r2s.str, a.r2s.r[0], a.r2s.r[1]);
			break;
		case VASM_OP_SET:
			teeprintf("\tset\tr%d,%s\n", a.rs.r, a.rs.str);
			break;
		case VASM_OP_PUSH:
			teeprintf("\tpush\tr%d\n", a.r.r);
			break;
		case VASM_OP_POP:
			teeprintf("\tpop\tr%d\n", a.r.r);
			break;
		case VASM_OP_ADD:
			teeprintf("\tadd\tr%d,r%d,r%d\n", a.r3.r[0], a.r3.r[1], a.r3.r[2]);
			break;
		}
	}
	fprintf(_f, "\n");
	for (size_t i = 0; i < stringcount; i++) {
		fprintf(_f, "__str_%d:\n"
		            "\t.long %lu\n"
		            "\t.str \"%s\"\n",
			    i, strlen(strings[i]), strings[i]);
	}
	printf("\n");

	/*
	// Write binary shit
	fd = open(argv[2], O_WRONLY | O_CREAT);
	write(fd, vbin, vbinlen);
	close(fd);
	*/

	// Yay
	return 0;
}

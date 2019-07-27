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


#define FUNC_LINE_NONE   0
#define FUNC_LINE_FUNC   1


#define VASM_OP_NONE     (-1)
#define VASM_OP_COMMENT  (-2)
#define VASM_OP_LABEL    (-3)
#define VASM_OP_NOP        0
#define VASM_OP_CALL       1
#define VASM_OP_LOAD        2
#define VASM_OP_PUSH       3
#define VAMS_OP_POP        4


struct variable {
	char type[32];
	char name[32];
};

struct func_line {
	char type;
};

struct func_line_func {
	struct func_line line;
	unsigned char paramcount;
	struct variable  assign;
	char name[32];
	char params[32][32];
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


struct vasm {
	short op;
	char _[14];
};

struct vasm_str {
	short op;
	char *str;
};

struct vasm_reg {
	short op;
	char  r;
};

struct vasm_reg2 {
	short op;
	char  r[2];
};

struct vasm_reg3 {
	short op;
	char  r[3];
};

struct vasm_reg_str {
	short op;
	char  r;
	char *str;
};


char *strings[4096];
size_t stringcount;

char *lines[4096];
size_t linecount;

struct func funcs[4096];
size_t funccount;

struct vasm vasms[4096];
size_t vasmcount;


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
	for ( ; ; k++) {
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
	for ( ; ; k++) {
		// Parse first 3 words for analysis
		char word[3][32];
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
		if (strcmp(word[0], "end") == 0) {
			break;
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
	union {
		struct vasm          a;
		struct vasm_str      as;
		struct vasm_reg      ar;
		struct vasm_reg2     ar2;
		struct vasm_reg3     ar3;
		struct vasm_reg_str  ars;
	} a;
	a.as.op  = VASM_OP_LABEL;
	a.as.str = f->name;
	vasms[vasmcount] = a.a;
	vasmcount++;
	for (size_t i = 0; i < f->linecount; i++) {
		struct func_line_func *flf;
		switch (f->lines[i]->type) {
		case FUNC_LINE_FUNC:
			flf = (struct func_line_func *)f->lines[i];
			size_t j = 0;
			for ( ; j < 16 && j < flf->paramcount; j++) {
				a.ars.op  = VASM_OP_LOAD;
				a.ars.r   = j;
				a.ars.str = flf->params[j];
				vasms[vasmcount] = a.a;
				vasmcount++;
			}
			for ( ; j < flf->paramcount; j++) {
				a.ars.op  = VASM_OP_LOAD;
				a.ars.r   = 16;
				a.ars.str = flf->params[j];
				vasms[vasmcount] = a.a;
				vasmcount++;
				a.ar.op = VASM_OP_PUSH;
				a.ar.r  = 16;
				vasms[vasmcount] = a.a;
				vasmcount++;
			}
			a.as.op  = VASM_OP_CALL;
			a.as.str = flf->name;
			vasms[vasmcount] = a.a;
			vasmcount++;
		}
	}
}


static int structs2vasm() {
	for (size_t i = 0; i < funccount; i++)
		func2vasm(&funcs[i]);
}



int main(int argc, char **argv) {
	
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
			struct func_line_func *flf;
			switch (f->lines[j]->type) {
			case FUNC_LINE_FUNC:
				flf = (struct func_line_func *)f->lines[j];
				printf("  Line:   Function\n");
				printf("  Assign: %s\n", flf->assign.name);
				printf("  Type:   %s\n", flf->assign.type);
				printf("  Name:   %s\n", flf->name);
				printf("  Params: %d\n", flf->paramcount);
				for (size_t k = 0; k < flf->paramcount; k++)
					printf("    %s\n", flf->params[k]);
				break;
			default:
				printf("  Unknown line type (%d)\n", f->lines[j]->type);
				abort();
			}
		}
	}
	printf("\n");

	// 3. Structs to virt assembly
	printf("=== structs2vasm ===\n");
	structs2vasm();
	printf("\n");
	for (size_t i = 0; i < vasmcount; i++) {
		union {
			struct vasm          a;
			struct vasm_str      as;
			struct vasm_reg      ar;
			struct vasm_reg2     ar2;
			struct vasm_reg3     ar3;
			struct vasm_reg_str  ars;
		} a;
		a.a = vasms[i];
		switch (a.a.op) {
		default:
			printf("Unknown OP (%d)\n", vasms[i].op);
			abort();
		case VASM_OP_NONE:
			printf("\n");
			break;
		case VASM_OP_COMMENT:
			for (size_t j = 0; j < vasmcount; j++) {
				if (vasms[j].op == VASM_OP_NONE || vasms[j].op == VASM_OP_COMMENT)
					continue;
				if (vasms[j].op == VASM_OP_LABEL)
					printf("# %s\n", a.as.str);
				else
					printf("\t# %s\n", a.as.str);
			}
			break;
		case VASM_OP_LABEL:
			printf("%s:\n", a.as.str);
			break;
		case VASM_OP_NOP:
			printf("\tnop\n");
			break;
		case VASM_OP_CALL:
			printf("\tcall\t%s\n", a.as.str);
			break;
		case VASM_OP_LOAD:
			printf("\tmov\tr%d,%s\n", a.ars.r, a.ars.str);
			break;
		case VASM_OP_PUSH:
			printf("\tpush\tr%d\n", a.ar.r);
			break;
		case VAMS_OP_POP:
			printf("\tpop\tr%d\n", a.ar.r);
			break;
		}
	}
	printf("\n");

	// Yay
	return 0;
}

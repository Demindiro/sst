#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include "vasm.h"
#include "func.h"
#include "lines.h"
#include "text2lines.h"
#include "lines2func.h"
#include "func2vasm.h"
#include "vasm2vbin.h"
#include "hashtbl.h"
#include "linkobj.h"
#include "util.h"
#include "optimize/lines.h"
#include "optimize/vasm.h"
#include "optimize/branch.h"



enum output {
	PROCESSED,
	IMMEDIATE,
	ASSEMBLY,
	OBJECT,
	RAW,
	EXECUTABLE,
} output_type = EXECUTABLE;

const char *output_file;
const char *input_file;
const char *libraries[256];
size_t      librarycount;


struct linerange {
	struct func  *func;
	const line_t *lines;
	size_t        count;
};



static int _text2lines(const char *buf,
                       line_t **lines , size_t *linecount,
		       char ***strings, size_t *stringcount)
{
	DEBUG("Converting text to lines");
	text2lines(buf, lines, linecount, strings, stringcount);
	line_t *l = *lines;
	for (size_t i = 0; i < *stringcount; i++)
		DEBUG(".str_%lu = \"%s\"", i, (*strings)[i]);
	for (size_t i = 0; i < *linecount; i++)
		DEBUG("%4u:%-2u (%6lu) | %s", l[i].pos.y, l[i].pos.x, l[i].pos.c, l[i].text);
	return 0;
}


static size_t _find_func_length(const line_t *lines, size_t linecount, const char *text)
{
	DEBUG("Searching for function boundaries");
	size_t nestlvl = 1, nestlines[256];
	size_t i = 0;
	for ( ; i < linecount; i++) {
		if (nestlvl == 0)
			break;
		const char *t = lines[i].text;
		if (streq(t, "end")) {
			nestlvl--;
		} else if (
		    strstart(t, "for "  ) ||
		    strstart(t, "if "   ) ||
		    strstart(t, "while ")) {
			nestlines[nestlvl] = i;
			nestlvl++;
		}
	}
	if (nestlvl > 0) {
		ERROR("Missing %lu 'end' statements", nestlvl);
		for (size_t j = 0; j < nestlvl; j++) {
			size_t k = nestlines[j];
			PRINTLINE(lines[k]);
		}	
		EXIT(1);
	}
	return i;
}


static void _include(const char *f, struct hashtbl *functbl,
                     struct linerange **funclns, size_t *funclncount,
		     struct hashtbl *incltbl);


static void _findboundaries(const line_t *lines, size_t linecount,
			    struct hashtbl *functbl,
                            struct linerange **funclns, size_t *funclncount,
			    struct hashtbl *incltbl,
                            const char *text)
{
	h_create(functbl, 4);
	DEBUG("Parsing lines to functions");
	DEBUG("Searching for function boundaries");
	for (size_t i = 0; i < linecount; i++) {
		line_t line = lines[i];
		if (strstart(line.text, "extern ")) {
			struct func *g = calloc(sizeof *g, 1);
			parsefunc_header(g, line, text);
			DEBUG("Adding external function '%s'", g->name);
			h_add(functbl, g->name, (size_t)g);
		} else if (strstart(line.text, "include ")) {
			char f[256];
			strncpy(f, line.text + strlen("include "), sizeof f); 
			_include(f, functbl, funclns, funclncount, incltbl);
		} else {
			struct func *g = calloc(sizeof *g, 1);
			parsefunc_header(g, line, text);
			DEBUG("Adding function '%s'", g->name);
			h_add(functbl, g->name, (size_t)g);
			i++;
			size_t l = _find_func_length(lines + i, linecount - i, text);
			(*funclns)[*funclncount].func  = g;
			(*funclns)[*funclncount].lines = lines + i;
			(*funclns)[*funclncount].count = l;
			(*funclncount)++;
			i += l - 1;
		}
	}
}


static void _include(const char *f, struct hashtbl *functbl,
                     struct linerange **funclns, size_t *funclncount,
		     struct hashtbl *incltbl)
{
	DEBUG("Including %s", f);
	char buf[0x10000];
	char *b = buf;
	for (const char *c = f; *c != 0; b++, c++) {
		if (*c == '.')
			*b = '/';
		else
			*b = *c;
	}
	strcpy(b, ".sst");
	char cwd[4096];
	getcwd(cwd, sizeof cwd);
	chdir("lib"); // TODO
	int fd = open(buf, O_RDONLY);
	if (fd == -1) {
		ERROR("Couldn't open '%s': %s", buf, strerror(errno));
		EXIT(1);
	}
	size_t n = read(fd, buf, sizeof buf - 1);
	buf[n] = 0;
	close(fd);
	chdir(cwd);

	char  **strings;
	line_t *lines;
	size_t stringcount, linecount;

	if (_text2lines(buf, &lines, &linecount, &strings, &stringcount) < 0) {
		ERROR("Failed text to lines stage");
		EXIT(1);
	}

	_findboundaries(lines, linecount, functbl,
			funclns, funclncount, incltbl, buf);
}


static void _lines2funcs(const line_t *lines, size_t linecount,
                         struct func **funcs, size_t *funccount,
                         struct hashtbl *functbl, const char *text)
{
	h_create(functbl, 4);
	struct linerange funclns[1024];
	size_t funclnc = 0;
	DEBUG("Parsing lines to functions");
	struct hashtbl incltbl;
	h_create(&incltbl, 4);
	struct linerange *_f = funclns;
	_findboundaries(lines, linecount, functbl,
	                &_f, &funclnc,
			&incltbl, text);
	DEBUG("%lu functions to be parsed", funclnc);
	DEBUG("Parsing lines between function boundaries");
	for (size_t i = 0; i < funclnc; i++) {
		struct linerange l = funclns[i];
		l.func->linecount = 0;
		DEBUG("  Parsing '%s'", l.func->name);
		lines2func(l.lines, l.count, l.func, functbl);
		int changed;
		do {
			changed = 0;
			DEBUG("  Applying basic optimization on '%s'", l.func->name);
			changed |= optimizefunc(l.func);
			DEBUG("  Applying branch optimization on '%s'", l.func->name);
			changed |= optimize_func_branches(l.func);
		} while (changed);
	}
	*funccount = funclnc;
	*funcs     = malloc(funclnc * sizeof **funcs);
	for (size_t i = 0; i < funclnc; i++)
		(*funcs)[i] = *funclns[i].func;
}




static void _print_usage(int argc, char **argv, int code)
{
	ERROR("Usage: %s <input> [-o <output>] [-cSiE]", argc > 0 ? argv[0] : "compiler");
	ERROR("     <input>    The file to generate the output from");
	ERROR("  -o <output>   The file to write the final binary to");
	ERROR("  -c            Output object file");
	ERROR("  -S            Output assembly file");
	ERROR("  -i            Output immediate file");
	ERROR("  -E            Output processed file");
	ERROR("  -L            Link object or library");
	EXIT(code);
}


static void _parse_args(int argc, char **argv)
{
	if (argc < 2)
		_print_usage(argc, argv, 2);
	for (size_t i = 1; i < argc; i++) {
		const char *v = argv[i];
		if (*v == '-') {
			v++;
			if (streq(v, "S")) {
				output_type = ASSEMBLY;
			} else if (streq(v, "c")) {
				output_type = OBJECT;
			} else if (streq(v, "E")) {
				output_type = PROCESSED;
			} else if (streq(v, "i")) {
				output_type = IMMEDIATE;
			} else if (streq(v, "o")) {
				i++;
				if (i >= argc) {
					ERROR("-o must be followed by a file path");
					EXIT(1);
				}
				output_file = argv[i];
			} else if (streq(v, "L")) {
				i++;
				if (i >= argc) {
					ERROR("-L must be followed by a file path");
					EXIT(1);
				}
				libraries[librarycount++] = argv[i];
			} else if (streq(v, "h") || streq(v, "-help")) {
				_print_usage(argc, argv, 0);
			} else {
				ERROR("Unknown option '%s'", v - 1);
				EXIT(1);
			}
		} else {
			if (input_file != NULL) {
				ERROR("You can only specify one input file");
				DEBUG("  Input file: '%s'", input_file);
				EXIT(1);
			}
			input_file = argv[i];
		}
	}
	if (input_file == NULL) {
		ERROR("No input file specified");
		EXIT(1);
	}
	if (output_file == NULL) {
		const char *b = strrchr(input_file, '/') + 1,
		           *e = strrchr(input_file, '.');
		char bi[1 << 12], bo[sizeof bi];
		memcpy(bi, b, e - b);
		bi[e - b] = 0;
		const char *fmt;
		switch (output_type) {
		case EXECUTABLE   : fmt = "%s"    ; break;
		case RAW      : fmt = "%s.bin"; break;
		case OBJECT   : fmt = "%s.sso"; break;
		case ASSEMBLY : fmt = "%s.ssa"; break;
		case IMMEDIATE: fmt = "%s.ssi"; break;
		case PROCESSED: fmt = "%s.sst"; break;
		}
		snprintf(bo, sizeof bo, fmt, bi);
		output_file = strclone(bo);
	}
	if (streq(input_file, output_file)) {
		ERROR("Input file cannot be the same as the output file");
		EXIT(1);
	}
}




int main(int argc, char **argv)
{
	_parse_args(argc, argv);

	// ALL THE WAAAY
	optimize_lines_options = -1;

	char  **strings;
	line_t *lines;
	size_t stringcount, linecount;
	struct func *funcs;
	size_t funccount;
	struct hashtbl functbl;

	// Read source
	char buf[0x10000];
	int fd = streq(input_file, "-") ? STDIN_FILENO : open(input_file, O_RDONLY);
	if (fd < 0)
		EXITERRNO("Failed to open input file", 1);
	size_t n = read(fd, buf, sizeof buf - 1);
	if (n == -1)
		EXITERRNO("Failed to read input file", 3);
	buf[n] = 0;
	close(fd);

	// Preprocess source to a more consistent format
	DEBUG("Preprocessing source");
	if (_text2lines(buf, &lines, &linecount, &strings, &stringcount) < 0) {
		ERROR("Failed text to lines stage");
		EXIT(1);
	}
	if (output_type == PROCESSED)
		goto end;

	// Convert source lines to immediate
	DEBUG("Converting source to immediate");
	_lines2funcs(lines, linecount, &funcs, &funccount, &functbl, buf);
	if (output_type == IMMEDIATE)
		goto end;

	// Convert immediate to assembly
	union vasm_all **vasms = malloc(funccount * sizeof *vasms);
	size_t *vasmcount = malloc(funccount * sizeof *vasmcount);
	DEBUG("Converting immediate to assembly");
	for (size_t i = 0; i < funccount; i++) {
		DEBUG("Converting '%s'", funcs[i].name);
		func2vasm(&vasms[i], &vasmcount[i], &funcs[i]);
		optimizevasm(vasms[i], &vasmcount[i]);
	}
	// Create extra assembly with string constants
	vasms     = realloc(vasms    , (funccount + 1) * sizeof *vasms    );
	vasmcount = realloc(vasmcount, (funccount + 1) * sizeof *vasmcount);
	vasms    [funccount] = malloc(stringcount * 3 * sizeof **vasms);
	vasmcount[funccount] = stringcount * 3;
	for (size_t i = 0; i < stringcount; i++) {
		union vasm_all a;
		char b[64];
		snprintf(b, sizeof b, "_str_%lu", i);
		DEBUG("%s = \"%s\"", b, strings[i]);
		size_t l = 0;
		int backslash = 0;
		for (const char *c = strings[i]; *c != 0; c++) {
			if (backslash || *c != '\\')
				l++, backslash = 0;
			else
				backslash = 1;
		}
		a.s.op  = OP_RAW_LONG;
		a.s.str = num2str(l);
		vasms[funccount][i * 3 + 0] = a;
		a.s.op  = OP_LABEL;
		a.s.str = strclone(b);
		vasms[funccount][i * 3 + 1] = a;
		a.s.op  = OP_RAW_STR;
		a.s.str = strings[i];
		vasms[funccount][i * 3 + 2] = a;
	}
	if (output_type == ASSEMBLY)
		goto end;

	// Convert assembly to binary
	char *vbins[1 << 5];
	size_t vbinlens[1 << 5];
	struct lblmap *maps = malloc((funccount + 1 + librarycount) * sizeof *maps);
	DEBUG("Converting assembly to binary");
	for (size_t i = 0; i < funccount + 1; i++) {
		vbins[i] = malloc(1 << 20);
		if (i != funccount)
			DEBUG("Assembling '%s'", funcs[i].name);
		else
			DEBUG("Assembling strings");
		vasm2vbin(vasms[i], vasmcount[i], vbins[i], &vbinlens[i], &maps[i]);
		vbins[i] = realloc(vbins[i], vbinlens[i]);
	}
	if (output_type == RAW || output_type == OBJECT)
		goto end;

	// Link binary
	char *vbin = malloc(1 << 20);
	size_t vbinlen;
	const char *v[1 << 5];
	for (size_t i = 0; i < funccount + 1; i++)
		v[i] = vbins[i];
	for (size_t i = 0; i < librarycount; i++) {
		size_t k = i + funccount + 1;
		int fd = open(libraries[i], O_RDONLY);
		if (fd < 0)
			EXITERRNO("Failed to open object", 1);
		char buf[1 << 16];
		size_t l = read(fd, buf, sizeof buf);
		if (l == -1)
			EXITERRNO("Failed to read object", 3);
		close(fd);
		vbins[k] = malloc(l);
		obj_parse(buf, l, vbins[k], &vbinlens[k], &maps[k]);
		vbins[k] = realloc(vbins[k], vbinlens[k]);
		v[k] = vbins[k];
	}
	DEBUG("Linking binary");
	linkobj(v, vbinlens, funccount + 1 + librarycount, maps, vbin, &vbinlen);
	vbin = realloc(vbin, vbinlen);
	if (output_type == EXECUTABLE)
		goto end;

	ERROR("This point should not be reachable.");
	EXIT(1);

end:
	// Write the output
	; FILE *_f = streq(output_file, "-") ? stdout : fopen(output_file, "w");
	if (_f == NULL)
		EXITERRNO("Failed to open output file", 1);

	if (output_type == EXECUTABLE) {
		DEBUG("Writing executable to '%s'", output_file);
		int fd = fileno(_f);
		write(fd, "\x55\x00\x20\x19", 4); // Magic number
		write(fd, vbin, vbinlen);
	} else if (output_type == OBJECT) {
		DEBUG("Writing object to '%s'", output_file);
		int fd = fileno(_f);
		write(fd, "\x55\x10\x20\x19", 4); // Magic number
		unsigned int l = (unsigned int)funccount;
		write(fd, &l, 4);
		for (size_t i = 0; i < funccount; i++) {
			dumplbl(fd, &maps[i]);
			unsigned int l = (unsigned int)vbinlens[i];
			write(fd, &l, 4);
			write(fd, vbins[i], vbinlens[i]);
		}
	} else if (output_type == RAW) {
		ERROR("Not implemented");
		EXIT(1);
	} else if (output_type == ASSEMBLY) {
		DEBUG("Writing assembly to '%s'", output_file);
		for (size_t h = 0; h < funccount + 1; h++) {
			for (size_t i = 0; i < vasmcount[h]; i++) {
				char buf[80];
				vasm2str(vasms[h][i], buf, sizeof buf);
				if (vasms[h][i].op != OP_LABEL)
					fprintf(_f, "\t%s\n", buf);
				else
					fprintf(_f, "%s\n", buf);
			}
			fprintf(_f, "\n");
		}
	} else if (output_type == IMMEDIATE) {
		DEBUG("Writing immediate to '%s'", output_file);
		for (size_t i = 0; i < funccount; i++) {
			if (i > 0)
				fprintf(_f, "\n\n");
			func f = &funcs[i];
			fprintf(_f, "# lines: %lu\n", f->linecount);
			fprintf(_f, "%s (", f->name);
			for (size_t j = 0; j < f->argcount; j++)
				fprintf(_f, "%s%s %s", j > 0 ? "," : "",
				        f->args[j].type, f->args[j].name);
			fprintf(_f, ") -> %s\n", f->type);
			for (size_t j = 0; j < f->linecount; j++) {
				char buf[256];
				line2str(f->lines[j], buf, sizeof buf);
				fprintf(_f, "\t%s\n", buf);
			}
		}
	} else if (output_type == PROCESSED) {
		DEBUG("Writing processed to '%s'", output_file);
		for (size_t i = 0; i < linecount; i++) {
			line_t l = lines[i];
			DEBUG("%-40s # %lu,%u,%u", l.text, l.pos.c, l.pos.y, l.pos.x);
		}
	}

	if (output_type == EXECUTABLE && !streq(output_file, "-"))
		chmod(output_file, 0766);

	return 0;
}

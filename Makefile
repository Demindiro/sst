CC = cc
LD = ld
AS = as

INCLUDE := -I include
CFLAGS   = -O0 -g -Wall
OUTPUT   = build

cc = $(CC) $(INCLUDE) $(CFLAGS) $+ -o $@
ld = $(LD) $< -o $@
as = $(AS) $< -o $@


all: compiler assembler interpreter linker dumper stdlib

clean:
	@echo Removing build directory
	@rm -rf build/

compiler:	build/compiler

assembler:	build/assembler

interpreter:	build/interpreter

linker:		build/linker

dumper:		build/dump

c2r: src/interpreter/cisc2risc.c src/vasm.c src/interpreter/syscall.c include/vasm.h
	$(CC) $(INCLUDE) $(CFLAGS) -T src/interpreter/cisc2risc.lds $+ -o $(OUTPUT)/interpreter

c2r64: src/interpreter/cisc2risc64.c src/vasm.c src/interpreter/syscall.c include/vasm.h
	#$(CC) $(INCLUDE) $(CFLAGS) $+ -o $(OUTPUT)/interpreter
	$(CC) $(INCLUDE) $(CFLAGS) -T src/interpreter/cisc2risc64.lds $+ -o $(OUTPUT)/interpreter

be2h: src/interpreter/be2h.c src/interpreter/syscall.c include/vasm.h
	$(CC) $(INCLUDE) $(CFLAGS) $+ -o $(OUTPUT)/interpreter

include std.mk
include test.mk


build/:
	@echo Creating build directory
	@[ -e build/ ] || mkdir build/

build/compiler:		src/compiler.c		src/text2lines.c	\
			src/lines2func.c	src/func2vasm.c		\
			src/hashtbl.c		src/optimize/lines.c	\
			src/optimize/vasm.c	src/func.c		\
			src/vasm.c		src/optimize/branch.c	\
			src/vasm2vbin.c		src/linkobj.c		\
			src/expr.c		src/var.c		\
			src/text2vasm.c		src/types.c		\
			include/util.h		include/vasm.h		\
			include/text2lines.h	include/func2vasm.h	\
			include/hashtbl.h	include/optimize/lines.h\
			include/optimize/vasm.h	include/func.h		\
			include/var.h		include/lines.h		\
			include/vasm2vbin.h	include/types.h
	@echo Building compiler
	@$(cc)

build/assembler:	src/assembler.c		src/vasm2vbin.c		\
			src/text2vasm.c		src/vasm.c		\
			include/vasm.h		include/vasm2vbin.h	\
			include/text2vasm.h
	@echo Building assembler
	@$(cc)

build/interpreter:	src/interpreter/base.c	src/interpreter/syscall.c\
			include/vasm.h
	@echo Building interpreter
	@$(cc)

build/linker:		src/linker.c		src/hashtbl.c		\
			include/hashtbl.h
	@echo Building linker
	@$(cc)

build/dump:		src/dump.c		src/vasm.c		\
			include/vasm.h
	@echo Building dumper
	@$(cc)

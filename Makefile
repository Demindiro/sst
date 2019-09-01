CC = cc
LD = ld
AS = as

INCLUDE := -Iinclude/
CFLAGS   = -O0 -g -Wall
OUTPUT   = build/

cc = $(CC) $(INCLUDE) $(CFLAGS) $+ -o $@
ld = $(LD) $< -o $@
as = $(AS) $< -o $@


all: compiler assembler interpreter linker stdlib

clean:
	@echo Removing build directory
	@rm -rf build/

compiler:	build/compiler

assembler:	build/assembler

interpreter:	build/interpreter

linker:		build/linker


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
			| build/					\
			include/util.h		include/vasm.h		\
			include/text2lines.h	include/func2vasm.h	\
			include/hashtbl.h	include/optimize/lines.h\
			include/optimize/vasm.h	include/func.h	
	@echo Building compiler
	@$(cc)

build/assembler:	src/assembler.c		src/vasm2vbin.c		\
			src/text2vasm.c					\
			| build/					\
			include/vasm.h		include/vasm2vbin.h	\
			include/text2vasm.h
	@echo Building assembler
	@$(cc)

build/interpreter:	src/interpreter.c				\
			| build/					\
			include/vasm.h
	@echo Building interpreter
	@$(cc)

build/linker:		src/linker.c		src/hashtbl.c		\
			| build/					\
			include/hashtbl.h
	@echo Building linker
	@$(cc)

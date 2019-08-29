CC = cc
LD = ld
AS = as

INCLUDE := -Iinclude/
CFLAGS   = -O0 -g -Wall

cc = $(CC) $(INCLUDE) $(CFLAGS) $+ -o $@
ld = $(LD) $< -o $@
as = $(AS) $< -o $@


default: build/compiler build/assembler build/interpreter build/linker


build/compiler:		src/compiler.c src/text2lines.c src/lines2func.c \
			src/func2vasm.c src/hashtbl.c \
			src/optimize/lines.c src/optimize/vasm.c | \
			include/util.h include/vasm.h include/text2lines.h \
			include/func2vasm.h include/hashtbl.h \
			include/optimize/lines.h include/optimize/vasm.h
	$(cc)

build/assembler:	src/assembler.c	src/vasm2vbin.c src/text2vasm.c | \
			include/vasm.h include/vasm2vbin.h include/text2vasm.h
	$(cc)

build/interpreter:	src/interpreter.c | \
			include/vasm.h
	$(cc)

build/linker:		src/linker.c src/hashtbl.c | \
			include/hashtbl.h
	$(cc)


test: default
	./build/compiler test/hello.sst /tmp/hello.ssa
	./build/assembler /tmp/hello.ssa /tmp/hello.sso
	./build/assembler test/writeln.ssa /tmp/writeln.sso
	./build/assembler test/_start.ssa /tmp/_start.sso
	./build/linker /tmp/hello.sso /tmp/writeln.sso /tmp/_start.sso /tmp/hello.ss
	./build/interpreter /tmp/hello.ss

test/_start.sso:
	./build/assembler   test/ssa/_start.ssa  /tmp/_start.sso

test-count: default test/_start.sso
	./build/compiler  test/sst/count.sst /tmp/count.ssa
	./build/assembler /tmp/count.ssa /tmp/count.sso
	./build/linker /tmp/_start.sso /tmp/count.sso /tmp/count.ss
	sh -c 'time ./build/interpreter /tmp/count.ss'

test-prime: default
	./build/compiler    test/sst/prime.sst   /tmp/prime.ssa
	./build/assembler   /tmp/prime.ssa       /tmp/prime.sso
	./build/assembler   test/ssa/writeln.ssa /tmp/writeln.sso
	./build/assembler   test/ssa/_start.ssa  /tmp/_start.sso
	./build/linker      /tmp/_start.sso      /tmp/prime.sso   /tmp/writeln.sso   /tmp/prime.ss
	sh -c 'time ./build/interpreter /tmp/prime.ss'

test-writeln_num: default
	./build/compiler	test/sst/writeln-num.sst	/tmp/writeln-num.ssa
	./build/assembler	/tmp/writeln-num.ssa		/tmp/writeln-num.sso
	./build/compiler	lib/std/io.sst			/tmp/writeln.ssa
	./build/assembler	/tmp/writeln.ssa		/tmp/writeln.sso
	./build/assembler	lib/std/core/io.ssa		/tmp/core_io.sso
	./build/assembler	lib/std/_start.ssa		/tmp/_start.sso
	./build/linker		/tmp/_start.sso /tmp/writeln-num.sso /tmp/writeln.sso \
	       			/tmp/core_io.sso \
				/tmp/writeln-num.ss
	./build/interpreter	/tmp/writeln-num.ss

test-readln: default
	./build/compiler    test/sst/readln.sst        /tmp/readln.ssa
	./build/assembler   /tmp/readln.ssa      /tmp/readln2.sso
	./build/assembler   test/ssa/readln.ssa  /tmp/readln.sso
	./build/assembler   test/ssa/writeln.ssa /tmp/writeln.sso
	./build/assembler   test/ssa/_start.ssa  /tmp/_start.sso
	./build/assembler   test/ssa/alloc.ssa   /tmp/alloc.sso
	./build/linker	    /tmp/_start.sso /tmp/writeln.sso /tmp/readln.sso /tmp/readln2.sso /tmp/alloc.sso /tmp/readln.ss
	./build/interpreter /tmp/readln.ss

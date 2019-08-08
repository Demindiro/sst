CC = cc
LD = ld
AS = as

INCLUDE = -Iinclude/
CFLAGS  = -O0 -g

cc = $(CC) $(INCLUDE) $(CFLAGS) $+ -o $@
ld = $(LD) $< -o $@
as = $(AS) $< -o $@


default: build/compiler build/assembler build/interpreter build/linker


build/compiler:		src/compiler.c src/text2lines.c src/lines2structs.c src/structs2vasm.c src/hashtbl.c | \
			include/vasm.h include/text2lines.h include/lines2structs.h include/structs2vasm.h include/hashtbl.h
	$(cc)

build/assembler:	src/assembler.c	src/vasm2vbin.c src/text2vasm.c | \
			include/vasm.h include/vasm2vbin.h include/text2vasm.h
	$(cc)

build/interpreter:	src/interpreter.c				| include/vasm.h
	$(cc)

build/linker:		src/linker.c src/hashtbl.c | include/hashtbl.h
	$(cc)


test: default
	./build/compiler test/hello.sst /tmp/hello.ssa
	./build/assembler /tmp/hello.ssa /tmp/hello.sso
	./build/assembler test/writeln.ssa /tmp/writeln.sso
	./build/assembler test/_start.ssa /tmp/_start.sso
	./build/linker /tmp/hello.sso /tmp/writeln.sso /tmp/_start.sso /tmp/hello.ss
	./build/interpreter /tmp/hello.ss

test-count: default
	./build/assembler test/count.ssa /tmp/count.sso
	./build/linker /tmp/count.sso /tmp/count.ss
	sh -c 'time ./build/interpreter /tmp/count.ss'

test-prime: default
	./build/compiler    test/sst/prime.sst   /tmp/prime.ssa
	./build/assembler   /tmp/prime.ssa       /tmp/prime.sso
	./build/assembler   test/ssa/writeln.ssa /tmp/writeln.sso
	./build/assembler   test/ssa/_start.ssa  /tmp/_start.sso
	./build/linker      /tmp/_start.sso      /tmp/prime.sso   /tmp/writeln.sso   /tmp/prime.ss
	sh -c 'time ./build/interpreter /tmp/prime.ss'

test-writeln-num: default
	./build/compiler    test/sst/writeln-num.sst   /tmp/writeln-num.ssa
	./build/assembler   /tmp/writeln-num.ssa       /tmp/writeln-num.sso
	./build/assembler   test/ssa/writeln.ssa /tmp/writeln.sso
	./build/assembler   test/ssa/_start.ssa  /tmp/_start.sso
	./build/linker      /tmp/_start.sso      /tmp/writeln-num.sso   /tmp/writeln.sso   /tmp/writeln-num.ss
	./build/interpreter /tmp/writeln-num.ss

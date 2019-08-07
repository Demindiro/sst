CC = cc
LD = ld
AS = as


cc = $(CC) $(INCLUDE) $(CFLAGS) $+ -o $@
ld = $(LD) $< -o $@
as = $(AS) $< -o $@


default: build/compiler build/assembler build/interpreter build/linker


build/compiler: src/compiler.c include/vasm.h
	cc src/compiler.c -Iinclude -o build/compiler -g

build/assembler: src/assembler.c include/vasm.h
	cc src/assembler.c -Iinclude -o build/assembler -g

build/interpreter: src/interpreter.c include/vasm.h
	cc src/interpreter.c -Iinclude -o build/interpreter -g

build/linker: src/linker.c include/vasm.h
	cc src/linker.c -Iinclude -o build/linker -g -O0


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
	./build/interpreter /tmp/prime.ss

test-writeln-num: default
	./build/compiler    test/sst/writeln-num.sst   /tmp/writeln-num.ssa
	./build/assembler   /tmp/writeln-num.ssa       /tmp/writeln-num.sso
	./build/assembler   test/ssa/writeln.ssa /tmp/writeln.sso
	./build/assembler   test/ssa/_start.ssa  /tmp/_start.sso
	./build/linker      /tmp/_start.sso      /tmp/writeln-num.sso   /tmp/writeln.sso   /tmp/writeln-num.ss
	./build/interpreter /tmp/writeln-num.ss

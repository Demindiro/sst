test-hello: all
	./build/compiler	test/hello/main.sst	/tmp/hello.ssa
	./build/assembler	/tmp/hello.ssa		/tmp/hello.sso
	./build/assembler 	test/writeln.ssa	/tmp/writeln.sso
	./build/assembler	test/_start.ssa		/tmp/_start.sso
	./build/linker		/tmp/hello.sso		/tmp/writeln.sso	\
				/tmp/_start.sso 				\
				/tmp/hello.ss
	./build/interpreter	/tmp/hello.ss

test-count: all
	./build/compiler	test/sst/count.sst	/tmp/count.ssa
	./build/assembler	/tmp/count.ssa		/tmp/count.sso
	./build/linker		/tmp/_start.sso		/tmp/count.sso		\
				/tmp/count.ss
	sh -c 'time ./build/interpreter /tmp/count.ss'

test-prime: all
	./build/compiler	test/sst/prime.sst	/tmp/prime.ssa
	./build/assembler	/tmp/prime.ssa		/tmp/prime.sso
	./build/linker		/tmp/std/_start.sso	/tmp/std/io.sso		/tmp/std/core/io.sso \
				/tmp/prime.sso \
				/tmp/prime.ss
	sh -c 'time ./build/interpreter /tmp/prime.ss'

test-writeln_num: all
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

test-readln: all
	./build/compiler	test/sst/readln.sst	/tmp/readln.ssa
	./build/assembler	/tmp/readln.ssa		/tmp/readln.sso
	./build/linker		/tmp/std/_start.sso	/tmp/std/io.sso		/tmp/std/core/io.sso \
				/tmp/readln.sso \
				/tmp/readln.ss
	./build/interpreter	/tmp/readln.ss

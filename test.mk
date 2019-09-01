SH = sh

TEST_LIB = -L $(STD_DIR)/_start.sso -L $(STD_DIR)/core/io.sso

test-hello: all
	./build/compiler	test/hello/main.sst	/tmp/hello.ssa
	./build/assembler	/tmp/hello.ssa		/tmp/hello.sso
	./build/assembler 	test/writeln.ssa	/tmp/writeln.sso
	./build/assembler	test/_start.ssa		/tmp/_start.sso
	./build/linker		/tmp/hello.sso		/tmp/writeln.sso	\
				/tmp/_start.sso 				\
				/tmp/hello.ss
	$(SH) -c './build/interpreter /tmp/hello.ss'

test-count: all
	$(SSC) $(TEST_LIB) test/sst/count.sst -o /tmp/count.ss
	$(SH) -c 'time ./build/interpreter /tmp/count.ss'

test-prime: all
	$(SSC) $(TEST_LIB) test/sst/prime.sst -o /tmp/prime.ss
	$(SH) -c 'time ./build/interpreter /tmp/prime.ss'

test-writeln_num: all
	$(SSC) $(TEST_LIB) test/sst/writeln-num.sst -o /tmp/writeln-num.ss
	$(SH) -c './build/interpreter /tmp/writeln-num.ss'

test-readln: all
	$(SSC) $(TEST_LIB) test/sst/readln.sst -o /tmp/readln.ss
	$(SH) -c './build/interpreter /tmp/readln.ss'

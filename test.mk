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
	$(SSC) $(TEST_LIB) test/count/main.sst -o /tmp/count.ss
	$(SH) -c 'time ./build/interpreter /tmp/count.ss'

test-prime-naive: all
	$(SSC) $(TEST_LIB) test/prime/naive.sst -o /tmp/prime.ss
	$(SH) -c 'time ./build/interpreter /tmp/prime.ss'

test-prime-fast: all
	$(SSC) $(TEST_LIB) test/prime/fast.sst -o /tmp/prime.ss
	$(SH) -c 'time ./build/interpreter /tmp/prime.ss'

test-writeln_num: all
	$(SSC) $(TEST_LIB) test/io/writeln-num.sst -o /tmp/writeln-num.ss
	$(SH) -c './build/interpreter /tmp/writeln-num.ss'

test-readln: all
	$(SSC) $(TEST_LIB) test/io/readln.sst -o /tmp/readln.ss
	$(SH) -c './build/interpreter /tmp/readln.ss'

test-guess-the-number:
	$(SSC) $(TEST_LIB) test/game/guess-the-number.sst -o /tmp/guess-the-number.ss
	$(SH) -c './build/interpreter /tmp/guess-the-number.ss'

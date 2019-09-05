SH = sh

TEST_LIB = -L $(STD_DIR)/_start.sso

_ssc := $(SSC) $(TEST_LIB)


test: test-basic test-performance test-io


test-basic: test-hello test-count

test-performance: test-prime-naive test-prime-fast

test-io: test-writeln_num 


test-hello: all
	$(_ssc) test/basic/hello.sst -o /tmp/hello.ss
	$(SH) -c './build/interpreter /tmp/hello.ss'

test-count: all
	$(_ssc) test/count/main.sst -o /tmp/count.ss
	$(SH) -c 'time ./build/interpreter /tmp/count.ss'

test-prime-naive: all
	$(_ssc) test/prime/naive.sst -o /tmp/prime.ss
	$(SH) -c 'time ./build/interpreter /tmp/prime.ss'

test-prime-fast: all
	$(_ssc) test/prime/fast.sst -o /tmp/prime-fast.ss
	$(SH) -c 'time ./build/interpreter /tmp/prime-fast.ss'

test-writeln_num: all
	$(_ssc) test/io/writeln-num.sst -o /tmp/writeln-num.ss
	$(SH) -c './build/interpreter /tmp/writeln-num.ss'

test-readln: all
	$(_ssc) test/io/readln.sst -o /tmp/readln.ss
	$(SH) -c './build/interpreter /tmp/readln.ss'

test-guess-the-number:
	$(_ssc) test/game/guess-the-number.sst -o /tmp/guess-the-number.ss
	$(SH) -c './build/interpreter /tmp/guess-the-number.ss'

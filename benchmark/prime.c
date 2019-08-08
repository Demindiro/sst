#include <stdio.h>


int main() {
	printf("%d\n", 2);
	printf("%d\n", 3);
	long i = 3;
#pragma clang loop vectorize(enable) interleave(enable)
	while(1) {
		for (size_t _ = 0; _ < 256; _++) {
		i += 2;
		if (i == 200001)
			return 0;
#pragma clang loop vectorize(enable) interleave(enable)
		for (size_t j = 2; j < i / 2; j++) {
			if (i % j == 0)
				goto nope;
		}
		printf("%d\n", i);
	nope:;
		}
	}
	return 0;
}

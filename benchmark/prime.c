#include <stdio.h>


int main() {
	printf("%d\n", 2);
	printf("%d\n", 3);
	long i = 3;
	while(1) {
		i += 2;
		if (i == 200001)
			return 0;
		for (size_t j = 2; j < i / 2; j++) {
			if (i % j == 0)
				goto nope;
		}
		printf("%ld\n", i);
	nope:;
	}
	return 0;
}

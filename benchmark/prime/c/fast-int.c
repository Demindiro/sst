#include <stdio.h>

void main()
{
	printf("2");
	printf("3");
	int i = 3;

	while (1) {

		i += 2;

		if (i == 2000001)
			break;

		for (int p = 3; p < i; p += 2) {
			if (i % p == 0)
				break;
			if (p * p >= i) {
				printf("%lu\n", i);
				break;
			}
		}
	}
}

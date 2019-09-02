#!/usr/bin/env python3

print(2)
print(3)
i = 3
while True:
    i = i + 2
    if i == 200001:
        break
    for j in range(2, i // 2):
        if i % j == 0:
            break
    else:
        print(i)

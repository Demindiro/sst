CC = cc
LD = ld
AS = as


cc = $(CC) $(INCLUDE) $(CFLAGS) $+ -o $@
ld = $(LD) $< -o $@
as = $(AS) $< -o $@


default: build/sst


build/sst: src/main.c
	cc src/main.c -o build/sst -g

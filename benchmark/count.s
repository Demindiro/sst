.global _start
_start:
	mov	$100000000,%rax
.for_0:
	test	%rax,%rax
	jz	.for_0_else
	dec	%rax
	jmp	.for_0
.for_0_else:
.for_0_end:
        mov     $60,%rax
	xor	%rdi,%rdi
	syscall


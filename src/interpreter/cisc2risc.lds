ENTRY(_start)

SECTIONS
{
	. = 0x100000;
	//.loop : { *(.loop) }
	//. = 0x10000;
	.text : { *(.text) }
	.data : { *(.data) }
	.bss : { *(.bss) }
}

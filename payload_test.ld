ENTRY(_start)
SECTIONS
{
	. = 0x85000000;
	.text : {
		*(.text.entry)
		*(.text*)
	}
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	.bss : { *(.bss*) }
}

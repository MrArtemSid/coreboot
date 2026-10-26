/* SPDX-License-Identifier: GPL-2.0-only */
OUTPUT_ARCH(aarch64)
ENTRY(_start)
PHDRS { payload PT_LOAD FLAGS(5); }
SECTIONS
{
	/* Runtime RAM [82408000,84a00000), above the entire declared
	 * ramstage [822f4000,832f4000) and cache [83380000,84380000).
	 * Never use the former 85000000 firmware-reserved placement.
	 */
	. = 0x84400000;
	.text : { *(.text.entry) *(.text*) } :payload
	.rodata : { *(.rodata*) } :payload
	.data : { *(.data*) } :payload
	.bss : { *(.bss*) *(COMMON) } :payload
	_payload_end = .;
	ASSERT(_start == 0x84400000, "unexpected payload entry")
	ASSERT(_payload_end <= 0x84500000, "payload exceeds audited safe window")
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

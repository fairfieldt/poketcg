/* static big-endian MIPS Linux executable for tools/endian_check.c (run with qemu-mips) */
ENTRY(__start)
SECTIONS
{
	. = 0x400000 + SIZEOF_HEADERS;
	.MIPS.abiflags : { *(.MIPS.abiflags) }
	.text : { *(.text .text.*) }
	.rodata : { *(.rodata .rodata.* .rodata1) }
	. = ALIGN(0x10000) + (. & 0xffff);
	.data : { *(.data .data.* .sdata .sdata.*) }
	.bss : { *(.sbss .sbss.* .bss .bss.* COMMON) }
	/DISCARD/ : { *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.mdebug.*) }
}

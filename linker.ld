ENTRY(_start)

SECTIONS
{
    /* 1MB adresine mühürle */
    . = 1M;

    .text : AT(ADDR(.text))
    {
        KEEP(*(.multiboot2))
        *(.text)
    }

    .rodata BLOCK(4K) : ALIGN(4K) { *(.rodata) }
    .data   BLOCK(4K) : ALIGN(4K) { *(.data) }
    .bss    BLOCK(4K) : ALIGN(4K) { *(COMMON) *(.bss) }
}

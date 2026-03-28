ENTRY(kernel_entry)

SECTIONS
{
    . = 0x0;

    /* Place the real entry first: the UEFI loader jumps to physical offset 0
       of the raw kernel blob, which must be `kernel_entry`, not other .text. */
    .text :
    {
        *(.text.kernel_entry)
        *(.text .text.*)
    }

    /* Writable data + BSS must be in the linked image so `objcopy -O binary`
       includes them and the UEFI loader allocates enough pages. */
    .data :
    {
        *(.data .data.*)
    }

    .bss :
    {
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame .gcc_except_table .comment .note* .debug*)
    }
}


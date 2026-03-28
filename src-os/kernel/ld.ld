ENTRY(kernel_entry)

/* One PT_LOAD so `llvm-objcopy -O binary` emits a contiguous image from VMA 0
   through the end of .bss. With split RX/RW segments, the second segment has
   FileSiz 0 for .NOBITS and llvm-objcopy only copies the first segment — then
   `g_idt` (in .bss) is missing from kernel.raw and Stage 2 hangs after the
   first print. */
PHDRS
{
    all PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x0;

    /* Place the real entry first: the UEFI loader jumps to physical offset 0
       of the raw kernel blob, which must be `kernel_entry`, not other .text. */
    .text :
    {
        *(.text.kernel_entry)
        *(.text .text.*)
    } :all

    .data :
    {
        *(.data .data.*)
    } :all

    .bss :
    {
        *(.bss .bss.*)
        *(COMMON)
    } :all

    /DISCARD/ : {
        *(.eh_frame .gcc_except_table .comment .note* .debug*)
    }
}


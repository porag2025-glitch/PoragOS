/*
 * linker.ld - PoragOS linker script
 *
 * Tells GNU ld how to arrange our object files into the final kernel image
 * and, critically, WHERE in memory the kernel expects to live.
 */

OUTPUT_FORMAT(elf32-i386)       /* Produce a 32-bit x86 ELF file */
OUTPUT_ARCH(i386)               /* Target architecture */
ENTRY(_start)                   /* First instruction to run (symbol from boot.asm) */

SECTIONS
{
    /* Place the kernel at 1 MiB (0x100000).
     * Below 1 MiB lives the BIOS, VGA memory, and other legacy areas
     * that we must not overwrite. */
    . = 1M;

    /* Marker symbol so C code can know where the kernel begins */
    kernel_start = .;

    /* Code. The Multiboot header MUST be first so it lies within the first 8 KiB of the file. */
    .text : ALIGN(4K)
    {
        *(.multiboot)           /* Multiboot header first */
        *(.text*)               /* Then all executable code */
    }

    /* Read-only data: string literals, constants */
    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    /* Initialised read/write data: globals with non-zero initial values */
    .data : ALIGN(4K)
    {
        *(.data*)
    }

    /* Uninitialised data and the stack. Occupies RAM but takes no file space. */
    .bss : ALIGN(4K)
    {
        *(COMMON)               /* Legacy "common" symbols */
        *(.bss*)                /* All .bss sections (includes our stack) */
    }

    /* Marker symbol for the end of the kernel image (useful for the memory manager later) */
    kernel_end = .;

    /* Throw away sections we do not need */
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.property)
    }
}

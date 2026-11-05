; gdt_flush.asm - load the new GDT and reload every segment register
bits 32

global gdt_flush

section .text
gdt_flush:
    mov eax, [esp + 4]      ; First C argument (cdecl): address of the gdt_ptr struct
    lgdt [eax]              ; Tell the CPU where the new GDT is

    mov ax, 0x10            ; 0x10 = selector of our kernel DATA descriptor
    mov ds, ax              ; Reload the data segment registers ...
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax              ; ... and the stack segment

    jmp 0x08:.reload_cs     ; Far jump: the ONLY way to reload CS. 0x08 = kernel CODE selector
.reload_cs:
    ret                     ; Back to C, now running on our own GDT

section .note.GNU-stack noalloc noexec nowrite progbits

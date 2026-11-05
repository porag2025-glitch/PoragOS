; =============================================================================
; paging_asm.asm - the two privileged operations that C cannot express
;
; Control registers involved:
;   CR3 = physical address of the page directory (the "root" of translation)
;   CR0 = bit 0  PE  Protected mode enable (GRUB already set it)
;         bit 16 WP  Write Protect: ring 0 also obeys read-only pages
;         bit 31 PG  Paging enable
; =============================================================================
bits 32

global paging_load_directory
global paging_enable

section .text

; void paging_load_directory(u32 page_directory_physical_address)
paging_load_directory:
    mov eax, [esp + 4]          ; First cdecl argument: physical address of the directory
    mov cr3, eax                ; Tell the MMU where the page tables start.
                                ; (Writing CR3 also flushes the TLB, the CPU's translation cache.)
    ret

; void paging_enable(void)
paging_enable:
    mov eax, cr0                ; Read the current control register 0
    or  eax, 0x80010000         ; Set PG (bit 31) and WP (bit 16)
    mov cr0, eax                ; From the NEXT instruction on, every address is translated.
                                ; We keep running because the code we execute is
                                ; identity-mapped: virtual address == physical address.
    ret                         ; The return address on the stack is also identity-mapped

section .note.GNU-stack noalloc noexec nowrite progbits

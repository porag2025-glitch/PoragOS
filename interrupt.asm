; =============================================================================
; interrupt.asm - IDT loader and the low-level interrupt entry stubs
;
; When an interrupt fires the CPU pushes EFLAGS, CS, EIP (plus an ERROR CODE for
; some exceptions) and jumps here. We cannot run C yet: registers must be saved
; and the stack must look the same for every vector. Each stub therefore:
;   1. pushes a dummy error code (if the CPU did not push one),
;   2. pushes its vector number,
;   3. jumps to isr_common, which saves state and calls the C dispatcher.
; =============================================================================
bits 32

extern isr_handler              ; C function in idt.c
global idt_flush
global isr_stub_table

section .text

; void idt_flush(u32 idt_ptr_addr)
idt_flush:
    mov eax, [esp + 4]          ; First argument: address of the idt_ptr struct
    lidt [eax]                  ; Load the IDT register
    ret

; --- Stub generators ---------------------------------------------------------
%macro ISR_NOERR 1              ; For vectors where the CPU pushes NO error code
isr_stub_%1:
    push dword 0                ; Dummy error code keeps the stack layout uniform
    push dword %1               ; Vector number
    jmp isr_common
%endmacro

%macro ISR_ERR 1                ; For vectors where the CPU DOES push an error code
isr_stub_%1:
    push dword %1               ; Vector number (error code is already on the stack)
    jmp isr_common
%endmacro

; --- CPU exceptions 0-31 -----------------------------------------------------
; Vectors 8, 10-14, 17, 21, 29, 30 push an error code; the rest do not.
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; --- Hardware IRQs 0-15 = vectors 32-47 (none push an error code) ------------
ISR_NOERR 32                    ; IRQ0  - PIT timer
ISR_NOERR 33                    ; IRQ1  - PS/2 keyboard
ISR_NOERR 34                    ; IRQ2  - cascade from slave PIC
ISR_NOERR 35                    ; IRQ3  - COM2
ISR_NOERR 36                    ; IRQ4  - COM1
ISR_NOERR 37                    ; IRQ5  - LPT2 / sound
ISR_NOERR 38                    ; IRQ6  - floppy
ISR_NOERR 39                    ; IRQ7  - LPT1 / spurious
ISR_NOERR 40                    ; IRQ8  - RTC
ISR_NOERR 41                    ; IRQ9
ISR_NOERR 42                    ; IRQ10
ISR_NOERR 43                    ; IRQ11
ISR_NOERR 44                    ; IRQ12 - PS/2 mouse
ISR_NOERR 45                    ; IRQ13 - FPU
ISR_NOERR 46                    ; IRQ14 - primary ATA
ISR_NOERR 47                    ; IRQ15 - secondary ATA

; --- Common path: save everything, call C, restore everything -----------------
isr_common:
    pushad                      ; Push EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI (in that order)
    mov ax, ds                  ; Get the current data segment ...
    push eax                    ; ... and save it (matches registers_t.ds)

    mov ax, 0x10                ; Kernel data selector
    mov ds, ax                  ; Make sure the kernel's segments are active
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld                         ; The C ABI requires the direction flag to be clear

    push esp                    ; Argument: pointer to the registers_t we just built
    call isr_handler            ; Run the C dispatcher
    add esp, 4                  ; Remove the argument

    pop eax                     ; Restore the saved data segment ...
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popad                       ; Restore the general-purpose registers
    add esp, 8                  ; Discard int_no and err_code
    iretd                       ; Pop EIP, CS, EFLAGS: resume the interrupted code

; --- Table of stub addresses, indexed by vector number ------------------------
section .rodata
isr_stub_table:
%assign i 0
%rep 48
    dd isr_stub_%+i             ; Emits isr_stub_0, isr_stub_1, ... isr_stub_47
%assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits

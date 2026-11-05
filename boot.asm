; =============================================================================
; boot.asm - PoragOS entry point (NASM, 32-bit, ELF output)
;
; Job of this file:
;   1. Provide a Multiboot header so GRUB recognises us as a bootable kernel.
;   2. Set up a stack (C code cannot run without one).
;   3. Call the C function kernel_main().
;   4. Halt the CPU forever if kernel_main() ever returns.
;
; State when GRUB jumps to us (guaranteed by the Multiboot 1 spec):
;   - CPU is in 32-bit protected mode, paging DISABLED
;   - EAX = 0x2BADB002 (proof that a Multiboot loader started us)
;   - EBX = physical address of the Multiboot information structure
;   - Interrupts are disabled (IF = 0)
;   - ESP is NOT valid -> we must set it ourselves
;   - Segment registers are flat 4GB segments (GRUB's temporary GDT)
; =============================================================================

bits 32                         ; Generate 32-bit instructions

; ---------------------------------------------------------------------------
; Multiboot header constants
; ---------------------------------------------------------------------------
MB_MAGIC    equ 0x1BADB002              ; Magic number GRUB scans for
MB_ALIGN    equ 1 << 0                  ; Flag bit 0: align loaded modules on 4KB pages
MB_MEMINFO  equ 1 << 1                  ; Flag bit 1: ask GRUB to provide memory info
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO   ; Combine the flags (= 0x3)
; Checksum: magic + flags + checksum must equal 0 (mod 2^32).
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

; ---------------------------------------------------------------------------
; Multiboot header section
; GRUB looks for this header in the first 8 KiB of the kernel file, 32-bit
; aligned. The linker script places ".multiboot" first to guarantee that.
; ---------------------------------------------------------------------------
section .multiboot align=4
    dd MB_MAGIC                 ; Offset 0: magic number
    dd MB_FLAGS                 ; Offset 4: feature flags
    dd MB_CHECKSUM              ; Offset 8: checksum

; ---------------------------------------------------------------------------
; Stack, reserved in the .bss section.
; .bss takes no space in the file; it is zero-filled memory at load time.
; The x86 stack grows DOWNWARD, so we point ESP at the TOP of this region.
; ---------------------------------------------------------------------------
section .bss align=16
stack_bottom:
    resb 16384                  ; Reserve 16 KiB for the kernel stack
stack_top:                      ; Label marking the highest address

; ---------------------------------------------------------------------------
; Code section
; ---------------------------------------------------------------------------
section .text
global _start                   ; Export _start so the linker can use it as entry
extern kernel_main              ; kernel_main is defined in kernel.c

_start:
    cli                         ; Make sure maskable interrupts are off (no IDT yet!)

    mov esp, stack_top          ; Point the stack pointer to our new stack

    ; Save the Multiboot values BEFORE we touch EAX/EBX.
    ; The C calling convention (cdecl, 32-bit) passes arguments on the stack,
    ; pushed right-to-left. We want: kernel_main(u32 magic, u32 mbi_addr)
    ; so we push mbi_addr (EBX) first, then magic (EAX).
    and esp, 0xFFFFFFF0         ; Align ESP to 16 bytes (the ABI expects this at call)
    sub esp, 8                  ; Pad so that after two 4-byte pushes ESP is 16-aligned
    push ebx                    ; 2nd argument: Multiboot info pointer
    push eax                    ; 1st argument: Multiboot magic value

    call kernel_main            ; Enter the C kernel

    ; If kernel_main returns, we land here. Halt forever.
    cli                         ; Disable interrupts so HLT is not woken up
.hang:
    hlt                         ; Stop the CPU until the next interrupt (none will come)
    jmp .hang                   ; Safety net in case of NMI / spurious wake-up

; Tell the linker this object does not need an executable stack
; (silences a GNU ld warning on modern toolchains).
section .note.GNU-stack noalloc noexec nowrite progbits

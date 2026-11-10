; =============================================================================
; boot.asm - PoragOS entry point (NASM, 32-bit, ELF output)
; =============================================================================

bits 32                         ; 32-বিট প্রটেক্টেড মোড নির্দেশাবলী

; ---------------------------------------------------------------------------
; Multiboot header constants (Video Framebuffer Request)
; ---------------------------------------------------------------------------
MB_MAGIC    equ 0x1BADB002              ; GRUB স্ক্যান করার জন্য ম্যাজিক নম্বর
MB_ALIGN    equ 1 << 0                  ; মডিউলগুলোকে ৪ কিলোবাইট পেজে অ্যালাইন করা
MB_MEMINFO  equ 1 << 1                  ; BIOS মেমরি ম্যাপ তথ্য চাওয়ার ফ্ল্যাগ
MB_VIDEO    equ 1 << 2                  ; গ্রাফিক্স মোড রিকোয়েস্ট করার ফ্ল্যাগ (Bit 2)
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO | MB_VIDEO    ; সম্মিলিত ফ্ল্যাগ মান = 0x7
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)  ; সঠিক চেকসাম হিসাব

; ---------------------------------------------------------------------------
; Multiboot header section with Graphics fields (Total 48 bytes)
; ---------------------------------------------------------------------------
section .multiboot align=4
    dd MB_MAGIC                 ; অফসেট ০: ম্যাজিক নম্বর
    dd MB_FLAGS                 ; অফসেট ৪: ফিচার ফ্ল্যাগ (0x7)
    dd MB_CHECKSUM              ; অফসেট ৮: চেকসাম
    dd 0, 0, 0, 0, 0            ; অফসেট ১২-৩১: header_addr, load_addr, load_end_addr, bss_end_addr, entry_addr (ELF-এর ক্ষেত্রে ০ থাকে)
    dd 0                        ; অফসেট ৩২: mode_type (0 = linear graphics mode)
    dd 1024                     ; অফসেট ৩৬: width (1024 pixels)
    dd 768                      ; অফসেট ৪০: height (768 pixels)
    dd 32                       ; অফসেট ৪৪: depth (32-bit color depth)

; ---------------------------------------------------------------------------
; Kernel Stack Allocation (.bss section)
; ---------------------------------------------------------------------------
section .bss align=16
stack_bottom:
    resb 16384                  ; কার্নেলের জন্য ১৬ কিলোবাইট স্ট্যাক রিজার্ভ করা হলো
stack_top:                      ; স্ট্যাকের শীর্ষ বা হাইয়েস্ট অ্যাড্রেস

; ---------------------------------------------------------------------------
; Kernel Entry Code Section
; ---------------------------------------------------------------------------
section .text
global _start                   ; লিংকারকে এন্ট্রি পয়েন্ট জানিয়ে দেওয়া
extern kernel_main              ; kernel.c ফাইলের মূল ফাংশন

_start:
    cli                         ; মাস্কেবল ইন্টারাপ্ট সাময়িকভাবে বন্ধ রাখা

    mov esp, stack_top          ; স্ট্যাক পয়েন্টারকে আমাদের নতুন স্ট্যাকের শীর্ষে সেট করা

    and esp, 0xFFFFFFF0         ; স্ট্যাককে ১৬-বাইটে পারফেক্টলি অ্যালাইন করা
    sub esp, 8                  ; দুটি ৪-বাইট আর্গুমেন্টের জন্য প্যাডিং তৈরি
    push ebx                    ; ২য় আর্গুমেন্ট: Multiboot info pointer (EBX)
    push eax                    ; ১ম আর্গুমেন্ট: Multiboot magic value (EAX)

    call kernel_main            ; সি কার্নেলের ভেতরে প্রবেশ করা

.hang:
    cli
    hlt
    jmp .hang                   ; কোনো কারণে কার্নেল রিটার্ন করলে সিস্টেম হোল্ড করে রাখা

section .note.GNU-stack noalloc noexec nowrite progbits

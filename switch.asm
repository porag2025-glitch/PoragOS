; =============================================================================
; switch.asm - low-level CPU context switch between two kernel tasks
;
; C prototype:
;   void context_switch(u32 *old_esp_ptr, u32 new_esp);
;
; Stack frame saved on the outgoing task's stack (6 dwords = 24 bytes):
;   [esp + 20]  return EIP  (pushed by `call context_switch`)
;   [esp + 16]  ebx
;   [esp + 12]  esi
;   [esp + 8 ]  edi
;   [esp + 4 ]  ebp
;   [esp + 0 ]  eflags      <-- saved into *old_esp_ptr
; =============================================================================
bits 32

global context_switch

section .text
context_switch:
    ; 1. Save outgoing task's callee-saved registers and EFLAGS on its own stack
    push ebx
    push esi
    push edi
    push ebp
    pushfd                      ; Save EFLAGS (especially bit 9, the Interrupt Flag)

    ; 2. Store the outgoing task's stack pointer into *old_esp_ptr
    ;    Offset math: 5 pushes (20 bytes) + return address (4 bytes) = 24
    mov eax, [esp + 24]         ; eax = old_esp_ptr (&prev->esp)
    mov [eax], esp              ; *old_esp_ptr = current ESP

    ; 3. Switch stacks to the incoming task!
    mov esp, [esp + 28]         ; esp = new_esp (next->esp)

    ; 4. Restore incoming task's EFLAGS and callee-saved registers from its stack
    popfd
    pop ebp
    pop edi
    pop esi
    pop ebx

    ; 5. Pop the incoming task's saved return address into EIP.
    ;    - For an existing task, this returns back into schedule().
    ;    - For a brand-new task, this jumps into task_trampoline()!
    ret

section .note.GNU-stack noalloc noexec nowrite progbits

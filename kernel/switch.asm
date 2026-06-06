[BITS 32]

; ============================================================
; context_switch(cpu_context_t *old, cpu_context_t *new)
;
; Çağrılma: context_switch(&prev->context, &next->context)
; [esp+0] = return addr
; [esp+4] = old*
; [esp+8] = new*
; ============================================================

[GLOBAL context_switch]
context_switch:
    ; Callee-saved register'ları stack'e kaydet
    push ebp
    push ebx
    push esi
    push edi

    ; old->esp = şu anki esp
    mov eax, [esp + 20]     ; old* (4 push + ret addr = 5*4 = 20)
    mov [eax], esp

    ; esp = new->esp
    mov eax, [esp + 24]     ; new* (4 push + ret addr + 1 arg = 24)
    mov esp, [eax]

    ; Yeni görevin register'larını geri yükle
    pop edi
    pop esi
    pop ebx
    pop ebp

    ret                     ; Yeni görevin EIP'sine dön

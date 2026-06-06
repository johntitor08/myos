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
    ; EFLAGS + callee-saved register'ları stack'e kaydet.
    ; EFLAGS'i görev başına koruyoruz ki interrupt-enable (IF) durumu
    ; switch boyunca doğru taşınsın (gerçek preemptive davranış).
    pushfd
    push ebp
    push ebx
    push esi
    push edi

    ; old->esp = şu anki esp
    mov eax, [esp + 24]     ; old* (5 push*4=20 + ret addr 4 = 24)
    mov [eax], esp

    ; esp = new->esp
    mov eax, [esp + 28]     ; new* (24 + 1 arg*4 = 28)
    mov esp, [eax]

    ; Yeni görevin register'larını geri yükle (kaydetmenin tersi sıra)
    pop edi
    pop esi
    pop ebx
    pop ebp
    popfd                   ; EFLAGS (IF dahil) geri yükle

    ret                     ; Yeni görevin EIP'sine dön

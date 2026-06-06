[BITS 32]

; ============================================================
; Kernel Entry Point (kernel_entry.asm)
; C kernel fonksiyonunu çağırır
; ============================================================

[EXTERN kernel_main]    ; C'deki kernel_main fonksiyonu

global _start
_start:
    call kernel_main    ; C kernel'ine geç
    jmp $               ; Kernel dönerse sonsuz döngü

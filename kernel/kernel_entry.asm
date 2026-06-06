[BITS 32]

; ============================================================
; Kernel Entry Point (kernel_entry.asm)
; .bss'i sıfırlar, sonra C kernel fonksiyonunu çağırır
; ============================================================

[EXTERN kernel_main]    ; C'deki kernel_main fonksiyonu
[EXTERN __bss_start]    ; linker.ld'den .bss sınırları
[EXTERN __bss_end]

global _start
_start:
    ; .bss'i sıfırla: flat binary'de .bss (NOBITS) imaja yazılmaz,
    ; bu yüzden RAM'de çöp içerir. C kodu global'lerin 0 olmasına güvenir
    ; (örn. win_count, pmm_used_frames, net = {0} ...). Önce temizle.
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi            ; ecx = bayt sayısı
    xor eax, eax
    cld
    rep stosb               ; [edi..edi+ecx) = 0

    call kernel_main        ; C kernel'ine geç
    jmp $                   ; Kernel dönerse sonsuz döngü

[BITS 16]
[ORG 0x7C00]

; ============================================================
; MyOS Stage 1 Bootloader - DÜZELTILMIŞ
; Sorun: dl register (drive number) BIOS tarafından otomatik
; set edilir, elle 0x80 yazmak yanlış olabilir.
; Çözüm: BIOS'un verdiği dl değerini kullan + hata durumunda
; retry yap.
; ============================================================

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; BIOS'un verdiği drive number'ı sakla
    mov [boot_drive], dl

    ; Ekranı temizle
    mov ax, 0x0003
    int 0x10

    mov si, msg_boot
    call print_string

    call load_kernel
    call enter_protected_mode
    jmp $

; ============================================================
; Kernel'i diskten oku (retry mekanizmalı)
; ============================================================
load_kernel:
    mov si, msg_loading
    call print_string

    ; Disk reset
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13

    mov cx, 3           ; 3 deneme hakkı
.retry:
    push cx

    mov ah, 0x02        ; BIOS read sectors
    mov al, 50          ; 50 sektör oku (25KB, kernel için yeterli)
    mov ch, 0           ; Silindir 0
    mov cl, 2           ; Sektör 2'den başla
    mov dh, 0           ; Kafa 0
    mov dl, [boot_drive] ; BIOS'un verdiği drive
    mov bx, 0x1000
    mov es, bx
    xor bx, bx
    int 0x13

    jnc .success        ; Carry=0 → başarılı

    ; Hata: disk reset + tekrar dene
    pop cx
    push cx
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13

    pop cx
    loop .retry

    ; 3 denemede olmadı
    mov si, msg_disk_error
    call print_string
    jmp $

.success:
    pop cx
    mov si, msg_ok
    call print_string
    ret

; ============================================================
; Protected Mode geçişi
; ============================================================
enter_protected_mode:
    mov si, msg_pmode
    call print_string

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp CODE_SEG:init_pm

[BITS 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    ; Kernel 0x10000 adresinde (es=0x1000, bx=0x0000 → 0x1000*16+0 = 0x10000)
    jmp 0x10000

; ============================================================
; 16-bit print
; ============================================================
[BITS 16]
print_string:
    mov ah, 0x0E
.loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .loop
.done:
    ret

; ============================================================
; GDT
; ============================================================
gdt_start:
gdt_null:
    dd 0x0
    dd 0x0
gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; ============================================================
; Mesajlar + veri
; ============================================================
boot_drive      db 0
msg_boot        db 'MyOS v2.0 Bootloader...', 0x0D, 0x0A, 0
msg_loading     db 'Kernel yukleniyor...', 0x0D, 0x0A, 0
msg_ok          db 'OK!', 0x0D, 0x0A, 0
msg_pmode       db 'Protected Mode...', 0x0D, 0x0A, 0
msg_disk_error  db 'HATA: Disk okunamadi! Drive=', 0

; ============================================================
; Boot imzası
; ============================================================
times 510 - ($ - $$) db 0
dw 0xAA55

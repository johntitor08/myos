[BITS 16]
[ORG 0x7C00]

; ============================================================
; MyOS Stage 1 Bootloader - DÜZELTILMIŞ
; Sorun: dl register (drive number) BIOS tarafından otomatik
; set edilir, elle 0x80 yazmak yanlış olabilir.
; Çözüm: BIOS'un verdiği dl değerini kullan + hata durumunda
; retry yap.
;
; Kernel yükleme: LBA -> CHS çevirisiyle sektör-sektör okunur.
; Tek int 0x13 ile 128 sektör okumak (a) 64KB sınırına takılır,
; (b) gerçek floppy'de track/head sınırını aştığı için BIOS'ta
; başarısız olur (QEMU hoşgörülüdür ama donanımda hang). Her sektör
; ayrı okunup ES paragraf-paragraf ilerletilerek 64KB DMA sınırı da
; tamamen aşılmaz; kernel boyutu artsa bile güvenle yüklenir.
;
; KERNEL_SECTORS Makefile tarafından kernel.bin boyutundan üretilir;
; tanımlı değilse güvenli bir varsayılan kullanılır.
; ============================================================

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 128
%endif

; 1.44MB floppy geometrisi
SECTORS_PER_TRACK equ 18
NUM_HEADS         equ 2

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

    ; Yükleme hedefi: es:bx = 0x1000:0x0000 = 0x10000
    ; Boot sektörü LBA 0; kernel LBA 1'den başlar.
    mov ax, 0x1000
    mov es, ax
    mov word [lba], 1
    mov cx, KERNEL_SECTORS      ; okunacak sektör sayısı

.load_loop:
    push cx
    call read_one_sector       ; [lba] -> es:0x0000
    pop cx

    ; ES'i 512 bayt (0x20 paragraf) ilerlet; bx hep 0 kalır, böylece
    ; tek okuma asla 64KB DMA sınırını aşmaz.
    mov ax, es
    add ax, 0x20
    mov es, ax
    inc word [lba]
    loop .load_loop

    mov si, msg_ok
    call print_string
    ret

; ============================================================
; Tek sektör oku: [lba] -> es:0x0000 (retry'li)
; LBA -> CHS:  sector   = (LBA % SPT) + 1
;              head     = (LBA / SPT) % HEADS
;              cylinder = (LBA / SPT) / HEADS
; Clobber: ax, bx, cx, dx, di
; ============================================================
read_one_sector:
    mov di, 3                  ; 3 deneme hakkı
.try:
    mov ax, [lba]
    xor dx, dx
    mov bx, SECTORS_PER_TRACK
    div bx                     ; ax = LBA/SPT, dx = LBA%SPT
    mov cl, dl
    inc cl                     ; cl = sektör (1 tabanlı)
    xor dx, dx
    mov bx, NUM_HEADS
    div bx                     ; ax = silindir, dx = kafa
    mov ch, al                 ; silindir (düşük 8 bit; bizim boyutta yeterli)
    mov dh, dl                 ; kafa
    mov dl, [boot_drive]
    mov ah, 0x02               ; BIOS: oku
    mov al, 1                  ; tek sektör
    xor bx, bx                 ; es:0x0000 (es çağıran tarafça ayarlı)
    int 0x13
    jnc .ok

    ; Hata: disk reset + tekrar dene
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    dec di
    jnz .try

    ; Denemeler tükendi
    mov si, msg_disk_error
    call print_string
    jmp $
.ok:
    ret

; ============================================================
; Protected Mode geçişi
; ============================================================
enter_protected_mode:
    mov si, msg_pmode
    call print_string

    ; A20 hattını aç (Fast A20, port 0x92).
    ; Olmadan adres biti 20 sıfırlanır ve 1MB üstü erişimler düşük
    ; belleğe sarar (gerçek donanımda bozulma/hang; QEMU varsayılan açar).
    in al, 0x92
    test al, 2
    jnz .a20_done       ; zaten açıksa dokunma
    or al, 2            ; A20 bitini set et
    and al, 0xFE        ; bit0 (fast reset) sıfır kalsın
    out 0x92, al
.a20_done:

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
lba             dw 0
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

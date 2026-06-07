[BITS 32]

; ============================================================
; ISR (Interrupt Service Routine) stub'ları
; Her ISR önce CPU register'larını kaydeder,
; sonra C handler'ı çağırır
; ============================================================

[EXTERN isr_handler]
[EXTERN irq_handler]

; IDT flush
[GLOBAL idt_flush]
idt_flush:
    mov eax, [esp+4]
    lidt [eax]
    ret

; ============================================================
; Makrolar: hata kodu olan/olmayan ISR'ler
; ============================================================
%macro ISR_NOERRCODE 1
[GLOBAL isr%1]
isr%1:
    cli
    push byte 0         ; dummy hata kodu
    push byte %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
[GLOBAL isr%1]
isr%1:
    cli
    push byte %1
    jmp isr_common_stub
%endmacro

%macro IRQ 2
[GLOBAL irq%1]
irq%1:
    cli
    push byte 0
    push byte %2
    jmp irq_common_stub
%endmacro

; ============================================================
; ISR tanımlamaları (0-31 CPU exception'ları)
; ============================================================
ISR_NOERRCODE 0   ; Division by Zero
ISR_NOERRCODE 1   ; Debug
ISR_NOERRCODE 2   ; Non Maskable Interrupt
ISR_NOERRCODE 3   ; Breakpoint
ISR_NOERRCODE 4   ; Into Detected Overflow
ISR_NOERRCODE 5   ; Out of Bounds
ISR_NOERRCODE 6   ; Invalid Opcode
ISR_NOERRCODE 7   ; No Coprocessor
ISR_ERRCODE   8   ; Double Fault
ISR_NOERRCODE 9   ; Coprocessor Segment Overrun
ISR_ERRCODE   10  ; Bad TSS
ISR_ERRCODE   11  ; Segment Not Present
ISR_ERRCODE   12  ; Stack Fault
ISR_ERRCODE   13  ; General Protection Fault
ISR_ERRCODE   14  ; Page Fault
ISR_NOERRCODE 15  ; Unknown Interrupt
ISR_NOERRCODE 16  ; Coprocessor Fault
ISR_NOERRCODE 17  ; Alignment Check
ISR_NOERRCODE 18  ; Machine Check
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

; ============================================================
; IRQ tanımlamaları (0-15 donanım interrupt'ları)
; ============================================================
IRQ 0,  32    ; Timer
IRQ 1,  33    ; Keyboard
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40    ; Real Time Clock
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44    ; Mouse
IRQ 13, 45
IRQ 14, 46    ; IDE Primary
IRQ 15, 47    ; IDE Secondary

; ============================================================
; Ortak ISR stub - register'ları kaydet, C handler çağır
; ============================================================
isr_common_stub:
    pusha               ; edi,esi,ebp,esp,ebx,edx,ecx,eax kaydet
    mov ax, ds
    push eax            ; data segment kaydet
    mov ax, 0x10        ; kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp            ; registers_t pointer'ı
    call isr_handler
    pop eax
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8          ; int_no ve err_code'u temizle
    iret                ; CS, EIP, EFLAGS, SS, ESP geri yükle

; ============================================================
; Ortak IRQ stub
; ============================================================
irq_common_stub:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call irq_handler
    pop eax
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    iret

; ============================================================
; INT 0x80 - Sistem çağrısı
; ============================================================
[EXTERN syscall_dispatch]

[GLOBAL isr128]
isr128:
    cli
    push byte 0
    push dword 128      ; int_no=128 (push byte 128 signed-byte taşardı)
    ; Ortak stub benzeri ama syscall_dispatch çağırır
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call syscall_dispatch
    ; Dönüş değerini saklanan EAX slotuna yaz.
    ; Stack: [esp+0]=ptr arg, +4=ds, +8..+36 = pusha (edi..eax),
    ; yani saklanan EAX [esp+36]'dadır ([esp+28] EDX idi -> hataliydi).
    mov [esp + 36], eax
    pop eax
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    sti
    iret

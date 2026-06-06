#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* IDT entry yapısı */
typedef struct __attribute__((packed)) {
    uint16_t base_low;      /* Handler adresinin alt 16 biti */
    uint16_t selector;      /* GDT code segment selector */
    uint8_t  zero;          /* Her zaman 0 */
    uint8_t  flags;         /* Tip ve özellikler */
    uint16_t base_high;     /* Handler adresinin üst 16 biti */
} idt_entry_t;

/* IDT pointer yapısı */
typedef struct __attribute__((packed)) {
    uint16_t limit;         /* IDT boyutu - 1 */
    uint32_t base;          /* IDT adresi */
} idt_ptr_t;

/* CPU register'ları (interrupt sırasında push edilen) */
typedef struct __attribute__((packed)) {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;             /* CPU otomatik push */
} registers_t;

/* Fonksiyon prototipleri */
void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

/* IRQ handler tipi */
typedef void (*irq_handler_t)(registers_t *);
void register_irq_handler(uint8_t irq, irq_handler_t handler);

/* ISR ve IRQ stub'ları (ASM'de tanımlı) */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

#endif

#include "../include/idt.h"
#include "../include/screen.h"
#include <stdint.h>

/* 256 IDT girişi */
static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;

/* IRQ handler tablosu */
static irq_handler_t irq_handlers[16] = {0};

/* Assembly'deki lidt fonksiyonu */
extern void idt_flush(uint32_t);

/* ============================================================
 * PIC'i yeniden programla
 * IRQ 0-7  → INT 32-39
 * IRQ 8-15 → INT 40-47
 * ============================================================ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void pic_remap(void) {
    /* Initialization command */
    outb(0x20, 0x11);  outb(0xA0, 0x11);
    /* Vector offset */
    outb(0x21, 0x20);  outb(0xA1, 0x28);
    /* Cascading */
    outb(0x21, 0x04);  outb(0xA1, 0x02);
    /* 8086 mode */
    outb(0x21, 0x01);  outb(0xA1, 0x01);
    /* Tüm IRQ'ları etkinleştir */
    outb(0x21, 0x00);  outb(0xA1, 0x00);
}

/* ============================================================
 * IDT gate'i ayarla
 * ============================================================ */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector  = sel;
    idt[num].zero      = 0;
    idt[num].flags     = flags | 0x60;
}

/* ============================================================
 * IDT'yi başlat
 * ============================================================ */
void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    /* Tüm girişleri sıfırla */
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    /* PIC'i yeniden programla */
    pic_remap();

    /* ISR'leri kaydet (CPU Exception handlers, 0-31) */
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);

    /* IRQ'ları kaydet (Donanım interrupt'ları, 32-47) */
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    /* IDT'yi yükle */
    idt_flush((uint32_t)&idt_ptr);
}

/* ============================================================
 * IRQ handler kaydet
 * ============================================================ */
void register_irq_handler(uint8_t irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
}

/* ============================================================
 * ISR handler (C tarafı) - exception'lar için
 * ============================================================ */
static const char *exception_msgs[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "Reserved"
};

void isr_handler(registers_t *regs) {
    screen_set_color(COLOR_WHITE, COLOR_RED);
    screen_println("");
    screen_println("  *** KERNEL PANIC ***  ");
    screen_print("  Exception: ");
    if (regs->int_no < 20)
        screen_println(exception_msgs[regs->int_no]);
    else
        screen_println("Unknown");
    screen_print("  Error Code: ");
    screen_print_hex(regs->err_code);
    screen_print("  EIP: ");
    screen_print_hex(regs->eip);
    screen_println("");
    __asm__ volatile ("hlt");
}

/* ============================================================
 * IRQ handler (C tarafı) - donanım interrupt'ları için
 * ============================================================ */
void irq_handler(registers_t *regs) {
    uint8_t irq = regs->int_no - 32;

    /* EOI (End of Interrupt) gönder */
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);

    /* Kayıtlı handler varsa çağır */
    if (irq_handlers[irq]) {
        irq_handlers[irq](regs);
    }
}

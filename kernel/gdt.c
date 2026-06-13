#include "../include/gdt.h"
#include "../include/screen.h"

/* ============================================================
 * GDT + TSS
 *
 * Bootloader yalnız null + ring-0 kod/veri içeren minimal bir GDT kurar.
 * Ring-3 çalıştırmak için kendi GDT'mizi kurarız:
 *   0x00 null
 *   0x08 kernel kod  (DPL0)   — bootloader ile bayt-bayt aynı
 *   0x10 kernel veri (DPL0)   — bootloader ile bayt-bayt aynı
 *   0x18 user   kod  (DPL3)
 *   0x20 user   veri (DPL3)
 *   0x28 TSS
 * TSS, ring-3 -> ring-0 geçişlerinde kullanılacak kernel stack'ini (esp0)
 * tutar. Descriptor kodlaması host testiyle (tests/gdt_sim.c) doğrulanır.
 * ============================================================ */

#define GDT_ENTRIES 6

typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;        /* üst limit nibble + bayraklar (G, D/B) */
    uint8_t  base_high;
} gdt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t;

/* 32-bit donanım TSS'i (yalnız esp0/ss0 kullanılır) */
typedef struct __attribute__((packed)) {
    uint32_t prev;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} tss_t;

static gdt_entry_t gdt[GDT_ENTRIES];
static tss_t       tss;
#ifndef GDT_HOST_TEST
static gdt_ptr_t   gdt_ptr;   /* yalnız gerçek kernel'de (gdt_init) kullanılır */
#endif

static void gdt_set(int i, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t flags) {
    gdt[i].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[i].base_low  = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid  = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].access    = access;
    gdt[i].gran      = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    gdt[i].base_high = (uint8_t)((base >> 24) & 0xFF);
}

/* Tablo + TSS içeriğini doldur (saf; host-testlenebilir). */
void gdt_build(void) {
    /* access: P|DPL|S|type. flags 0xC0 => G(4K)+D/B(32-bit) => gran 0xCF */
    gdt_set(0, 0, 0,         0x00, 0x00);   /* null */
    gdt_set(1, 0, 0xFFFFF,   0x9A, 0xC0);   /* kernel kod  */
    gdt_set(2, 0, 0xFFFFF,   0x92, 0xC0);   /* kernel veri */
    gdt_set(3, 0, 0xFFFFF,   0xFA, 0xC0);   /* user kod  (DPL3) */
    gdt_set(4, 0, 0xFFFFF,   0xF2, 0xC0);   /* user veri (DPL3) */

    /* TSS descriptor: access 0x89 (P + type 9 = 32-bit TSS available),
     * byte granularity (flags 0). */
    /* (unsigned long) ara dökümü: gerçek kernel'de 32-bit (sorunsuz), host
     * testinde 64-bit pointer'ı işaretçi-tam-sayı uyarısı vermeden daraltır
     * (test base baytlarını kontrol etmez). */
    uint32_t base  = (uint32_t)(unsigned long)&tss;
    uint32_t limit = sizeof(tss) - 1;
    gdt_set(5, base, limit, 0x89, 0x00);

    /* TSS'i sıfırla; ring-0 stack segmenti + IO map base ayarla */
    uint8_t *t = (uint8_t *)&tss;
    for (unsigned i = 0; i < sizeof(tss); i++) t[i] = 0;
    tss.ss0        = SEL_KDATA;
    tss.esp0       = 0;               /* görev başına güncellenir */
    tss.iomap_base = sizeof(tss);     /* IO izin haritası yok */
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

#ifndef GDT_HOST_TEST

static inline void gdt_flush(uint32_t ptr) {
    __asm__ volatile (
        "lgdt (%0)\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        "ljmp $0x08, $1f\n\t"   /* kod segmentini yeniden yükle */
        "1:\n\t"
        : : "r"(ptr) : "ax", "memory"
    );
}

static inline void tss_flush(void) {
    __asm__ volatile ("movw $0x28, %%ax\n\t ltr %%ax" ::: "ax");
}

void enter_usermode(uint32_t entry, uint32_t user_stack) {
    __asm__ volatile (
        "cli\n\t"
        "movw $0x23, %%ax\n\t"     /* user veri seçicisi (RPL3) */
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "pushl $0x23\n\t"          /* SS  */
        "pushl %[ustack]\n\t"      /* ESP */
        "pushfl\n\t"
        "popl %%eax\n\t"
        "orl $0x200, %%eax\n\t"    /* IF=1: ring-3'te interrupt açık */
        "pushl %%eax\n\t"          /* EFLAGS */
        "pushl $0x1B\n\t"          /* CS (user kod, RPL3) */
        "pushl %[uentry]\n\t"      /* EIP */
        "iret\n\t"
        : : [ustack]"r"(user_stack), [uentry]"r"(entry) : "eax", "memory"
    );
}

void gdt_init(void) {
    gdt_build();
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;
    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
    screen_println("[GDT] GDT+TSS kuruldu (ring-3 segmentleri hazir).");
}

#endif /* GDT_HOST_TEST */

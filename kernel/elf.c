#include "../include/elf.h"
#include "../include/memory.h"
#include "../include/screen.h"
#include "../include/paging.h"

/* ============================================================
 * ELF32 doğrula
 * ============================================================ */
int elf_validate(const uint8_t *data, uint32_t size) {
    if (size < sizeof(elf32_header_t)) return -1;
    const elf32_header_t *hdr = (const elf32_header_t *)data;

    if (hdr->magic   != ELF_MAGIC) { screen_println("[ELF] Gecersiz magic!"); return -1; }
    if (hdr->bits    != 1)         { screen_println("[ELF] 64-bit desteklenmiyor!"); return -1; }
    if (hdr->machine != EM_386)    { screen_println("[ELF] x86 degil!"); return -1; }
    if (hdr->type    != ET_EXEC)   { screen_println("[ELF] Executable degil!"); return -1; }
    return 0;
}

/* ============================================================
 * ELF32 yükle — PT_LOAD segmentlerini belleğe kopyala
 * ============================================================ */
int elf_load(const uint8_t *data, uint32_t size, elf_program_t *out) {
    if (elf_validate(data, size) != 0) return -1;

    const elf32_header_t *hdr = (const elf32_header_t *)data;
    out->entry = hdr->entry;
    out->valid = 0;

    screen_print("[ELF] Entry: "); screen_print_hex(hdr->entry);
    screen_print(" Segments: "); screen_print_int(hdr->ph_count);
    screen_putchar('\n');

    /* Program header'ları işle */
    for (int i = 0; i < hdr->ph_count; i++) {
        const elf32_phdr_t *ph = (const elf32_phdr_t *)
            (data + hdr->ph_offset + i * hdr->ph_entry_size);

        if (ph->type != PT_LOAD) continue;

        screen_print("[ELF] Segment yükleniyor: vaddr=");
        screen_print_hex(ph->vaddr);
        screen_print(" size="); screen_print_int((int32_t)ph->mem_size);
        screen_putchar('\n');

        /* Hedef belleği sıfırla (BSS için) */
        memset((void *)ph->vaddr, 0, ph->mem_size);

        /* Dosyadan kopyala */
        if (ph->file_size > 0) {
            memcpy((void *)ph->vaddr, data + ph->offset, ph->file_size);
        }
    }

    /* User stack: 8MB adresinde 16KB */
    uint32_t user_stack = 0x800000;
    uint8_t *stack_mem = (uint8_t *)kmalloc(16384);
    if (stack_mem) {
        memset(stack_mem, 0, 16384);
        out->stack = user_stack + 16384 - 4;
    } else {
        out->stack = 0;
    }

    out->valid = 1;
    return 0;
}

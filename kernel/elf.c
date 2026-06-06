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

    /* Program header tablosu dosya içinde mi? (taşma korumalı kontroller)
     * Tüm offset/boyut alanları saldırgan kontrolündedir; her birini
     * 'size' tamponuna ve izinli yükleme penceresine göre doğrula. */
    if (hdr->ph_entry_size < sizeof(elf32_phdr_t)) {
        screen_println("[ELF] Gecersiz ph_entry_size!"); return -1;
    }
    if (hdr->ph_count > ELF_MAX_PHDRS) {
        screen_println("[ELF] Cok fazla program header!"); return -1;
    }
    /* ph_offset + ph_count*ph_entry_size <= size, taşmasız */
    uint32_t ph_table_bytes = (uint32_t)hdr->ph_count * hdr->ph_entry_size;
    if (ph_table_bytes / hdr->ph_entry_size != hdr->ph_count ||      /* çarpım taşması */
        hdr->ph_offset > size || ph_table_bytes > size - hdr->ph_offset) {
        screen_println("[ELF] Program header tablosu dosya disinda!"); return -1;
    }

    /* Program header'ları işle */
    for (int i = 0; i < hdr->ph_count; i++) {
        const elf32_phdr_t *ph = (const elf32_phdr_t *)
            (data + hdr->ph_offset + i * hdr->ph_entry_size);

        if (ph->type != PT_LOAD) continue;

        screen_print("[ELF] Segment yükleniyor: vaddr=");
        screen_print_hex(ph->vaddr);
        screen_print(" size="); screen_print_int((int32_t)ph->mem_size);
        screen_putchar('\n');

        /* Kaynak aralığı dosya içinde mi? offset + file_size <= size (taşmasız) */
        if (ph->offset > size || ph->file_size > size - ph->offset) {
            screen_println("[ELF] Segment kaynagi dosya disinda!"); return -1;
        }
        /* file_size, mem_size'i asamaz */
        if (ph->file_size > ph->mem_size) {
            screen_println("[ELF] file_size > mem_size!"); return -1;
        }
        /* Hedef [vaddr, vaddr+mem_size) izinli yükleme penceresinde mi?
         * (taşmasız) Kernel/IDT/page-table gibi adreslere yazmayı engeller. */
        if (ph->vaddr < ELF_LOAD_MIN || ph->vaddr >= ELF_LOAD_MAX ||
            ph->mem_size > ELF_LOAD_MAX - ph->vaddr) {
            screen_println("[ELF] Segment hedefi izinli aralik disinda!"); return -1;
        }

        /* Hedef belleği sıfırla (BSS için) */
        memset((void *)ph->vaddr, 0, ph->mem_size);

        /* Dosyadan kopyala */
        if (ph->file_size > 0) {
            memcpy((void *)ph->vaddr, data + ph->offset, ph->file_size);
        }
    }

    /* User stack: 8MB bölgesinde 16KB. Bu alan PMM'de rezerve ve
     * identity-map'li (bkz. paging.c yerleşimi), bu yüzden doğrudan
     * kullanılır — önceki kodun kmalloc'ladığı tampon kullanılmadan
     * sızıyordu ve asıl stack sıfırlanmıyordu. */
    uint32_t user_stack = 0x800000;
    memset((void *)user_stack, 0, 16384);
    out->stack = user_stack + 16384 - 4;

    out->valid = 1;
    return 0;
}

#include "../include/paging.h"
#include "../include/memory.h"
#include "../include/screen.h"

/* ============================================================
 * Physical Memory Manager (PMM)
 * Bitmap tabanlı: her bit = 1 fiziksel frame (4KB)
 * 128MB fiziksel bellek destekler (32768 frame)
 * ============================================================ */

#define PMM_MEMORY_SIZE     (128 * 1024 * 1024)   /* 128MB */
#define PMM_FRAME_COUNT     (PMM_MEMORY_SIZE / PAGE_SIZE)
#define PMM_BITMAP_SIZE     (PMM_FRAME_COUNT / 32)

static uint32_t pmm_bitmap[PMM_BITMAP_SIZE];
static uint32_t pmm_used_frames = 0;

/* Bitmap bit işlemleri */
static void bitmap_set(uint32_t frame) {
    pmm_bitmap[frame / 32] |= (1U << (frame % 32));
}
static void bitmap_clear(uint32_t frame) {
    pmm_bitmap[frame / 32] &= ~(1U << (frame % 32));
}
static int bitmap_test(uint32_t frame) {
    return (pmm_bitmap[frame / 32] >> (frame % 32)) & 1;
}

/* ============================================================
 * Frame ayır
 * ============================================================ */
uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < PMM_FRAME_COUNT; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            pmm_used_frames++;
            /* Fiziksel adres = frame index × 4KB */
            uint32_t phys = i * PAGE_SIZE;
            /* Belleği sıfırla */
            memset((void *)phys, 0, PAGE_SIZE);
            return phys;
        }
    }
    screen_println("[PMM] KRITIK: Fiziksel bellek bitti!");
    return 0;
}

/* ============================================================
 * Frame serbest bırak
 * ============================================================ */
void pmm_free_frame(uint32_t phys_addr) {
    uint32_t frame = phys_addr / PAGE_SIZE;
    if (frame < PMM_FRAME_COUNT && bitmap_test(frame)) {
        bitmap_clear(frame);
        pmm_used_frames--;
    }
}

/* ============================================================
 * PMM'i başlat: kernel alanını işaretle (0–4MB dolu)
 * ============================================================ */
static void pmm_init(void) {
    /* Tümünü boş işaretle */
    memset(pmm_bitmap, 0, sizeof(pmm_bitmap));

    /* İlk 4MB'ı dolu işaretle (kernel + low memory) */
    for (uint32_t i = 0; i < (4 * 1024 * 1024) / PAGE_SIZE; i++) {
        bitmap_set(i);
    }
    pmm_used_frames = (4 * 1024 * 1024) / PAGE_SIZE;

    screen_print("[PMM] Toplam: ");
    screen_print_int(PMM_FRAME_COUNT);
    screen_print(" frame, Kullanilan (kernel): ");
    screen_print_int((int32_t)pmm_used_frames);
    screen_println(" frame");
}

/* ============================================================
 * Kernel page directory (global)
 * ============================================================ */
static page_directory_t *kernel_dir = 0;

/* ============================================================
 * Yeni page directory oluştur
 * ============================================================ */
page_directory_t *paging_create_directory(void) {
    page_directory_t *dir = (page_directory_t *)pmm_alloc_frame();
    memset(dir, 0, sizeof(page_directory_t));
    return dir;
}

/* ============================================================
 * Sanal → Fiziksel adresi eşle
 * ============================================================ */
void paging_map(page_directory_t *dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_idx = virt >> 22;              /* Üst 10 bit = PD index */
    uint32_t pt_idx = (virt >> 12) & 0x3FF;   /* Orta 10 bit = PT index */

    /* Page table yoksa oluştur */
    if (!(dir->entries[pd_idx] & PAGE_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_frame();
        dir->entries[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    /* Page table'a eriş */
    page_table_t *pt = (page_table_t *)(dir->entries[pd_idx] & ~0xFFF);
    pt->entries[pt_idx] = (phys & ~0xFFF) | flags | PAGE_PRESENT;
}

/* ============================================================
 * Sanal adresi kaldır
 * ============================================================ */
void paging_unmap(page_directory_t *dir, uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    if (!(dir->entries[pd_idx] & PAGE_PRESENT)) return;

    page_table_t *pt = (page_table_t *)(dir->entries[pd_idx] & ~0xFFF);
    if (pt->entries[pt_idx] & PAGE_PRESENT) {
        pt->entries[pt_idx] = 0;
        /* TLB'yi geçersiz kıl */
        __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

/* ============================================================
 * Sanal → Fiziksel çeviri
 * ============================================================ */
uint32_t paging_get_physical(page_directory_t *dir, uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    if (!(dir->entries[pd_idx] & PAGE_PRESENT)) return 0;
    page_table_t *pt = (page_table_t *)(dir->entries[pd_idx] & ~0xFFF);
    if (!(pt->entries[pt_idx] & PAGE_PRESENT)) return 0;
    return (pt->entries[pt_idx] & ~0xFFF) | (virt & 0xFFF);
}

/* ============================================================
 * Page directory'yi CR3'e yükle (context switch)
 * ============================================================ */
void paging_switch(page_directory_t *dir) {
    __asm__ volatile (
        "mov %0, %%cr3\n"
        : : "r"((uint32_t)dir) : "memory"
    );
}

/* ============================================================
 * Identity mapping: virt == phys (0 → 4MB)
 * ============================================================ */
static void identity_map_kernel(page_directory_t *dir) {
    for (uint32_t addr = 0; addr < 4 * 1024 * 1024; addr += PAGE_SIZE) {
        paging_map(dir, addr, addr, PAGE_PRESENT | PAGE_WRITABLE);
    }
}

/* ============================================================
 * Paging'i başlat ve etkinleştir
 * ============================================================ */
void paging_init(void) {
    pmm_init();

    /* Kernel page directory oluştur */
    kernel_dir = paging_create_directory();

    /* İlk 4MB'ı identity map et (kernel kodu burada) */
    identity_map_kernel(kernel_dir);

    /* VGA belleğini de map et (0xB8000) */
    paging_map(kernel_dir, 0xB8000, 0xB8000, PAGE_PRESENT | PAGE_WRITABLE);

    /* Page directory'yi etkinleştir */
    paging_switch(kernel_dir);

    /* CR0'da paging bitini set et */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    screen_println("[PAGING] Sanal bellek aktif! Identity map: 0-4MB");
}

/* ============================================================
 * Page fault handler
 * ============================================================ */
void page_fault_handler(uint32_t err_code, uint32_t cr2) {
    screen_set_color(COLOR_WHITE, COLOR_RED);
    screen_println("");
    screen_println("  *** PAGE FAULT ***  ");
    screen_print("  Adres: ");
    screen_print_hex(cr2);
    screen_print("  Hata: ");
    if (err_code & 1)  screen_print("koruma ihlali ");
    else               screen_print("sayfa yok ");
    if (err_code & 2)  screen_print("(yazma) ");
    else               screen_print("(okuma) ");
    if (err_code & 4)  screen_print("[kullanici modu]");
    else               screen_print("[kernel modu]");
    screen_println("");
    screen_set_color(COLOR_WHITE, COLOR_BLACK);
    __asm__ volatile ("hlt");
}

#ifndef PAGING_H
#define PAGING_H

#include "stdint.h"

/* Sayfa boyutu: 4KB */
#define PAGE_SIZE       4096
#define PAGE_ENTRIES    1024

/* Page Directory / Table entry bayrakları */
#define PAGE_PRESENT    (1 << 0)   /* Sayfa bellekte */
#define PAGE_WRITABLE   (1 << 1)   /* Yazılabilir */
#define PAGE_USER       (1 << 2)   /* Kullanıcı alanı (ring3) */
#define PAGE_ACCESSED   (1 << 5)   /* CPU erişti */
#define PAGE_DIRTY      (1 << 6)   /* CPU yazdı */

/* Adres hizalama */
#define PAGE_ALIGN(x)   (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(x) ((x) & ~(PAGE_SIZE - 1))

/* Page Directory Entry */
typedef uint32_t pde_t;
/* Page Table Entry */
typedef uint32_t pte_t;

/* Page Directory: 1024 entry × 4 byte = 4KB */
typedef struct {
    pde_t entries[PAGE_ENTRIES];
} __attribute__((aligned(4096))) page_directory_t;

/* Page Table: 1024 entry × 4 byte = 4KB */
typedef struct {
    pte_t entries[PAGE_ENTRIES];
} __attribute__((aligned(4096))) page_table_t;

/* Fonksiyonlar */
void paging_init(void);
void paging_map(page_directory_t *dir, uint32_t virt, uint32_t phys, uint32_t flags);
void paging_unmap(page_directory_t *dir, uint32_t virt);
uint32_t paging_get_physical(page_directory_t *dir, uint32_t virt);
void paging_switch(page_directory_t *dir);
page_directory_t *paging_create_directory(void);
void paging_clone_directory(page_directory_t *dst, page_directory_t *src);

/* Physical memory frame allocator */
uint32_t pmm_alloc_frame(void);
void     pmm_free_frame(uint32_t frame);

/* Page fault handler */
void page_fault_handler(uint32_t err_code, uint32_t cr2);

#endif

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

/* Heap başlangıç adresi (1MB + biraz üstü) */
#define HEAP_START      0x100000
#define HEAP_SIZE       (4 * 1024 * 1024)   /* 4MB heap */

/* Bellek bloğu header */
typedef struct mem_block {
    size_t          size;
    uint8_t         is_free;
    struct mem_block *next;
    struct mem_block *prev;
    uint32_t        magic;      /* 0xDEADBEEF - bozulma tespiti */
} mem_block_t;

/* Fonksiyonlar */
void  memory_init(void);
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t new_size);
void  kfree(void *ptr);
void  memory_print_stats(void);

/* Bellek yardımcıları */
void *memset(void *ptr, int value, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
int   memcmp(const void *s1, const void *s2, size_t count);

#endif

#include "../include/memory.h"
#include "../include/screen.h"
#include "../include/critical.h"

#define BLOCK_MAGIC     0xDEADBEEF
#define BLOCK_HEADER    sizeof(mem_block_t)

static mem_block_t *heap_start_block = NULL;

/* ============================================================
 * memset / memcpy / memcmp implementasyonları
 * ============================================================ */
void *memset(void *ptr, int value, size_t count) {
    uint8_t *p = (uint8_t *)ptr;
    while (count--) *p++ = (uint8_t)value;
    return ptr;
}

void *memcpy(void *dest, const void *src, size_t count) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (count--) *d++ = *s++;
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t count) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    while (count--) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return 0;
}

/* ============================================================
 * Heap'i başlat
 * ============================================================ */
void memory_init(void) {
    heap_start_block = (mem_block_t *)HEAP_START;
    heap_start_block->size    = HEAP_SIZE - BLOCK_HEADER;
    heap_start_block->is_free = 1;
    heap_start_block->next    = NULL;
    heap_start_block->prev    = NULL;
    heap_start_block->magic   = BLOCK_MAGIC;
}

/* ============================================================
 * Boş blokları birleştir (coalescing)
 * ============================================================ */
static void merge_free_blocks(mem_block_t *block) {
    /* Sonraki blok da boşsa birleştir */
    if (block->next && block->next->is_free) {
        block->size += BLOCK_HEADER + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }
    /* Önceki blok da boşsa birleştir */
    if (block->prev && block->prev->is_free) {
        block->prev->size += BLOCK_HEADER + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

/* ============================================================
 * kmalloc - kernel bellek ayırıcı (first-fit)
 * ============================================================ */
void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* 8-byte hizalama */
    size = (size + 7) & ~7;

    /* Free-list IRQ-güvenli: handler içinden de güvenle çağrılabilsin */
    uint32_t f = irq_save();
    mem_block_t *current = heap_start_block;

    while (current) {
        if (current->magic != BLOCK_MAGIC) {
            irq_restore(f);
            screen_println("[MEMORY] Heap corruption detected!");
            return NULL;
        }

        if (current->is_free && current->size >= size) {
            /* Blok ikiye bölünecek kadar büyükse böl */
            if (current->size >= size + BLOCK_HEADER + 8) {
                mem_block_t *new_block = (mem_block_t *)((uint8_t *)current + BLOCK_HEADER + size);
                new_block->size    = current->size - size - BLOCK_HEADER;
                new_block->is_free = 1;
                new_block->next    = current->next;
                new_block->prev    = current;
                new_block->magic   = BLOCK_MAGIC;
                if (current->next) current->next->prev = new_block;
                current->next = new_block;
                current->size = size;
            }
            current->is_free = 0;
            void *ret = (void *)((uint8_t *)current + BLOCK_HEADER);
            irq_restore(f);
            return ret;
        }
        current = current->next;
    }

    irq_restore(f);
    screen_println("[MEMORY] Out of memory!");
    return NULL;
}

/* ============================================================
 * kcalloc - sıfırlanmış bellek ayır
 * ============================================================ */
void *kcalloc(size_t count, size_t size) {
    /* count * size taşmasını engelle (CWE-190) */
    if (size != 0 && count > ((size_t)-1) / size) return NULL;
    size_t total = count * size;
    void *ptr = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

/* ============================================================
 * krealloc - bloğu yeniden boyutlandır
 * ============================================================ */
void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }

    mem_block_t *block = (mem_block_t *)((uint8_t *)ptr - BLOCK_HEADER);
    if (block->size >= new_size) return ptr;

    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

/* ============================================================
 * kfree - belleği serbest bırak
 * ============================================================ */
void kfree(void *ptr) {
    if (!ptr) return;

    mem_block_t *block = (mem_block_t *)((uint8_t *)ptr - BLOCK_HEADER);
    uint32_t f = irq_save();
    if (block->magic != BLOCK_MAGIC) {
        irq_restore(f);
        screen_println("[MEMORY] Double free or corruption!");
        return;
    }
    block->is_free = 1;
    merge_free_blocks(block);
    irq_restore(f);
}

/* ============================================================
 * Bellek istatistiklerini yazdır
 * ============================================================ */
/* İstatistikleri çalışırken heap'i gezerek hesapla. Önceki sürüm
 * total_allocated/total_free sayaçlarını artımlı tutuyordu ama bölme/
 * birleştirmede geri kazanılan/harcanan BLOCK_HEADER baytlarını
 * hesaba katmadığı için zamanla kayıyordu. Doğruluk kaynağı blok
 * listesinin kendisidir; meminfo seyrek çağrıldığından O(n) gezinti
 * sorun değil. En büyük boş blok aynı zamanda parçalanma göstergesidir. */
void memory_print_stats(void) {
    size_t used = 0, free = 0, largest_free = 0;
    int blocks = 0;

    uint32_t f = irq_save();
    for (mem_block_t *b = heap_start_block; b; b = b->next) {
        if (b->magic != BLOCK_MAGIC) {
            irq_restore(f);
            screen_println("[MEMORY] Heap corruption detected!");
            return;
        }
        blocks++;
        if (b->is_free) {
            free += b->size;
            if (b->size > largest_free) largest_free = b->size;
        } else {
            used += b->size;
        }
    }
    irq_restore(f);

    screen_print("[MEMORY] Allocated: ");
    screen_print_int((int32_t)used);
    screen_print(" B | Free: ");
    screen_print_int((int32_t)free);
    screen_print(" B | Blocks: ");
    screen_print_int(blocks);
    screen_print(" | Largest free: ");
    screen_print_int((int32_t)largest_free);
    screen_println(" B");
}

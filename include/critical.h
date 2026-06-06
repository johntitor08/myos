#ifndef CRITICAL_H
#define CRITICAL_H

#include "stdint.h"

/* ============================================================
 * Tek-CPU kritik bölge: interrupt'ları kapat/aç.
 * EFLAGS korunup geri yüklendiği için iç içe (nested) kullanımda
 * dış bağlamın IF (interrupt enable) durumu bozulmaz:
 *   uint32_t f = irq_save();   ... kritik bölge ...   irq_restore(f);
 * ============================================================ */
static inline uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushf\n\t"
                      "pop %0\n\t"
                      "cli"
                      : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags) {
    __asm__ volatile ("push %0\n\t"
                      "popf"
                      :: "r"(flags) : "memory", "cc");
}

#endif

#ifndef GDT_H
#define GDT_H

#include "stdint.h"

/* Segment seçicileri (selector = index<<3 | RPL) */
#define SEL_KCODE   0x08   /* index 1, RPL0 */
#define SEL_KDATA   0x10   /* index 2, RPL0 */
#define SEL_UCODE   0x1B   /* index 3, RPL3 */
#define SEL_UDATA   0x23   /* index 4, RPL3 */
#define SEL_TSS     0x28   /* index 5, RPL0 */

/* GDT + TSS kur ve yükle (bootloader'ın minimal GDT'sinin yerine geçer;
 * kernel kod/veri seçicileri 0x08/0x10 ile birebir aynı kalır, ek olarak
 * ring-3 kod/veri ve bir TSS eklenir). */
void gdt_init(void);

/* TSS.esp0'i ayarla: ring-3 -> ring-0 geçişinde (syscall/IRQ/fault) CPU
 * bu kernel stack'ine geçer. Zamanlayıcı görev başına günceller. */
void tss_set_kernel_stack(uint32_t esp0);

/* Ring-3'e geç: verilen entry/stack ile kullanıcı moduna iret yapar.
 * Geri dönmez (kullanıcı programı syscall exit ile çıkar). */
void enter_usermode(uint32_t entry, uint32_t user_stack);

#endif

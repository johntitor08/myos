#include "../include/timer.h"
#include "../include/idt.h"
#include "../include/screen.h"
#include "../include/task.h"

/* PIT (Programmable Interval Timer) */
#define PIT_CHANNEL0    0x40
#define PIT_CMD         0x43
#define PIT_BASE_FREQ   1193180   /* Hz */
#define TIMER_FREQ      100       /* Hedef: 100Hz (10ms per tick) */

/* IRQ0 handler ile timer_wait/timer_get_ticks arasında paylaşılır:
 * volatile olmazsa busy-wait döngüsü okumayı önbelleğe alıp asılabilir. */
static volatile uint32_t timer_ticks = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ============================================================
 * IRQ0 handler: sistem saati
 * ============================================================ */
static void timer_handler(registers_t *regs) {
    (void)regs;
    timer_ticks++;
    task_tick();   /* Zamanlayıcıya tick ver */
}

/* ============================================================
 * PIT'i başlat
 * ============================================================ */
void timer_init(void) {
    uint32_t divisor = PIT_BASE_FREQ / TIMER_FREQ;

    /* Mode 3: Square wave generator */
    outb(PIT_CMD, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    register_irq_handler(0, timer_handler);

    screen_print("[TIMER] PIT baslatildi: ");
    screen_print_int(TIMER_FREQ);
    screen_println(" Hz");
}

/* ============================================================
 * Mevcut tick sayısını döndür
 * ============================================================ */
uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

/* ============================================================
 * Busy-wait gecikme (yaklaşık ms)
 * ============================================================ */
void timer_wait(uint32_t ms) {
    uint32_t end = timer_ticks + (ms * TIMER_FREQ / 1000);
    while (timer_ticks < end) {
        __asm__ volatile ("hlt");
    }
}

#include "../include/syscall.h"
#include "../include/screen.h"
#include "../include/task.h"
#include "../include/fs.h"
#include "../include/memory.h"
#include "../include/timer.h"
#include "../include/idt.h"

/* ============================================================
 * Sistem Çağrıları
 * Kullanıcı alanı: int 0x80 → EAX=syscall no
 *                             EBX=arg1, ECX=arg2, EDX=arg3
 * ============================================================ */

/* SYS_EXIT (0): process sonlandır */
static uint32_t sys_exit(uint32_t code, uint32_t a2, uint32_t a3, uint32_t a4) {
    (void)a2; (void)a3; (void)a4;
    screen_print("[SYSCALL] exit("); screen_print_int((int32_t)code); screen_println(")");
    task_exit();
    return 0;
}

/* SYS_WRITE (1): ekrana yaz (fd=1 → stdout) */
static uint32_t sys_write(uint32_t fd, uint32_t buf_addr, uint32_t len, uint32_t a4) {
    (void)fd; (void)a4;
    const char *buf = (const char *)buf_addr;
    for (uint32_t i = 0; i < len; i++)
        screen_putchar(buf[i]);
    return len;
}

/* SYS_READ (2): klavyeden oku */
static uint32_t sys_read(uint32_t fd, uint32_t buf_addr, uint32_t len, uint32_t a4) {
    (void)fd; (void)a4;
    char *buf = (char *)buf_addr;
    uint32_t i = 0;
    /* Basit: fs_read ile dosya oku (fd > 2) veya stdin */
    (void)buf; (void)i; (void)len;
    return 0;
}

/* SYS_GETPID (5) */
static uint32_t sys_getpid(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    task_t *t = task_current();
    return t ? t->pid : 0;
}

/* SYS_SLEEP (6): ms uyut */
static uint32_t sys_sleep(uint32_t ms, uint32_t a2, uint32_t a3, uint32_t a4) {
    (void)a2; (void)a3; (void)a4;
    task_sleep(ms);
    return 0;
}

/* SYS_SBRK (9): heap büyüt */
static uint32_t sys_sbrk(uint32_t size, uint32_t a2, uint32_t a3, uint32_t a4) {
    (void)a2; (void)a3; (void)a4;
    void *ptr = kmalloc(size);
    return (uint32_t)ptr;
}

/* SYS_UPTIME (10) */
static uint32_t sys_uptime(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    return timer_get_ticks();
}

/* Syscall tablosu */
static syscall_fn_t syscall_table[MAX_SYSCALLS] = {
    [SYS_EXIT]   = sys_exit,
    [SYS_WRITE]  = sys_write,
    [SYS_READ]   = sys_read,
    [SYS_GETPID] = sys_getpid,
    [SYS_SLEEP]  = sys_sleep,
    [SYS_SBRK]   = sys_sbrk,
    [SYS_UPTIME] = sys_uptime,
};

/* ============================================================
 * INT 0x80 handler
 * ============================================================ */
uint32_t syscall_dispatch(registers_t *regs) {
    uint32_t num = regs->eax;
    if (num >= MAX_SYSCALLS || !syscall_table[num]) {
        screen_print("[SYSCALL] Bilinmeyen: "); screen_print_int((int32_t)num);
        screen_putchar('\n');
        return (uint32_t)-1;
    }
    return syscall_table[num](regs->ebx, regs->ecx, regs->edx, regs->esi);
}

/* INT 0x80 için ISR stub */
extern void isr128(void);

void syscall_init(void) {
    /* IDT'ye INT 0x80 ekle — ring 3'ten çağrılabilir (DPL=3) */
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0xEE);
    screen_println("[SYSCALL] INT 0x80 hazir. 7 sistem cagrisi aktif.");
}

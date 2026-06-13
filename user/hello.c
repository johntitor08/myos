/* ============================================================
 * hello.c — MyOS userland örnek programı (ring-3)
 *
 * ulib.h üzerinden birden çok syscall kullanır: write, getpid, uptime,
 * exit. Çekirdek başlığı yoktur; user.ld ile 0x500000'e linklenip ET_EXEC
 * ELF32 üretir. Çekirdek 'run hello' ile yükleyip ring-3'te koşar.
 * ============================================================ */
#include "ulib.h"

void _start(void) {
    u_print("Merhaba ring-3 dunyasi! (kullanici modu)\n");
    u_printnum("  PID            : ", (unsigned)u_getpid());
    u_printnum("  Uptime (tick)  : ", (unsigned)u_uptime());

    /* Interaktif: foreground terminal sayesinde klavye bu programa ait,
     * shell girdiyi çalmaz. */
    char name[64];
    u_print("Adin ne? ");
    int n = u_read(name, sizeof(name));
    u_print("Merhaba, ");
    if (n > 0) u_write(1, name, n);
    u_print("! Kullanici programi cikiyor.\n");
    u_exit(0);
}

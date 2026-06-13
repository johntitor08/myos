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
    u_print("Kullanici programi cikiyor.\n");
    u_exit(0);
}

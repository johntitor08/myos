/* ============================================================
 * hello.c — MyOS userland örnek programı (ring-3)
 *
 * Çekirdek bağımlılığı yok; yalnız int 0x80 syscall ABI'si kullanılır:
 *   eax = syscall no, ebx/ecx/edx/esi = arg1..4
 *   SYS_EXIT=0, SYS_WRITE=1
 * user.ld ile 0x500000 (ELF yükleme penceresi) tabanına linklenir ve
 * ET_EXEC ELF32 üretir; çekirdek 'run hello' ile yükleyip ring-3'te koşar.
 * ============================================================ */

static int sys_write(int fd, const char *buf, int len) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(1), "b"(fd), "c"(buf), "d"(len)
        : "memory");
    return ret;
}

static void sys_exit(int code) {
    __asm__ volatile ("int $0x80" : : "a"(0), "b"(code));
    for (;;) { }   /* exit dönmez; yine de güvenli kal */
}

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void _start(void) {
    const char *msg = "Merhaba ring-3 dunyasi! (kullanici modu)\n";
    sys_write(1, msg, slen(msg));
    sys_exit(0);
}

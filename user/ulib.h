/* ============================================================
 * ulib.h — MyOS userland mini kütüphanesi (ring-3)
 *
 * int 0x80 syscall ABI'si etrafında ince sarmalayıcılar. Çekirdek başlığı
 * içermez; kullanıcı programları yalnız bununla derlenir.
 *   eax = no, ebx/ecx/edx = arg1..3
 * ============================================================ */
#ifndef ULIB_H
#define ULIB_H

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_GETPID  5
#define SYS_SLEEP   6
#define SYS_UPTIME  10

static inline int u_syscall(int n, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a1), "c"(a2), "d"(a3)
        : "memory");
    return ret;
}

static inline int u_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static inline int  u_write(int fd, const char *b, int l) { return u_syscall(SYS_WRITE, fd, (int)b, l); }
static inline void u_print(const char *s)                { u_write(1, s, u_strlen(s)); }
static inline int  u_read(char *b, int l)                { return u_syscall(SYS_READ, 0, (int)b, l); }
static inline int  u_getpid(void)                        { return u_syscall(SYS_GETPID, 0, 0, 0); }
static inline int  u_uptime(void)                        { return u_syscall(SYS_UPTIME, 0, 0, 0); }
static inline void u_sleep(int ms)                       { u_syscall(SYS_SLEEP, ms, 0, 0); }
static inline void u_exit(int c)                         { u_syscall(SYS_EXIT, c, 0, 0); for (;;) { } }

/* işaretsiz int -> ondalık string (buf >= 11 bayt) */
static inline void u_utoa(unsigned v, char *buf) {
    char tmp[11]; int i = 0;
    if (!v) { buf[0] = '0'; buf[1] = 0; return; }
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0; while (i) buf[j++] = tmp[--i]; buf[j] = 0;
}
static inline void u_printnum(const char *label, unsigned v) {
    char n[12]; u_utoa(v, n); u_print(label); u_print(n); u_print("\n");
}

#endif

#ifndef KSTRING_H
#define KSTRING_H

#include "stdint.h"

/* ============================================================
 * Çekirdek içi basit string yardımcıları (libc yok).
 * static inline: her çeviri birimi kendi kopyasını alır, bu yüzden
 * çoklu-tanım (multiple definition) çakışması olmaz ve kullanılmayan
 * fonksiyonlar uyarı üretmez.
 * Daha önce kstrcpy/kstrcmp/kstrlen/katoi shell/fs/task içinde ayrı ayrı
 * (üstelik farklı imzalarla) kopyalanmıştı; tek kaynak burada.
 * ============================================================ */

static inline int kstrlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static inline int kstrcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* Sınırlı kopya: en fazla max-1 bayt yazar + NUL sonlandırır.
 * max <= 0 ise hiçbir şey yazmaz. (Eski shell kstrcpy'si sınırsızdı.) */
static inline void kstrncpy(char *d, const char *s, int max) {
    int i = 0;
    if (max <= 0) return;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static inline int32_t katoi(const char *s) {
    int32_t n = 0, neg = 1;
    if (*s == '-') { neg = -1; s++; }
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n * neg;
}

#endif

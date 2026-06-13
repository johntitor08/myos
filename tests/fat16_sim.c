/* ============================================================
 * fat16_sim.c — FAT16 salt-okunur sürücü doğrulaması
 *
 * Gerçek fs/fat16.c'yi derler ve ATA'yı, mkfs.fat ile üretilmiş GERÇEK bir
 * FAT16 imajıyla taklit eder (referans araç = güçlü doğrulama). Mount eder,
 * mcopy ile konan HELLO.TXT'i okur ve beklenen içerikle karşılaştırır;
 * büyük/küçük harf isim ve olmayan dosya da sınanır.
 *
 * Kullanım: fat16_sim <fat16.img> <beklenen-icerik-dosyasi>
 * Çıkış: 0 = geçti, !=0 = hata.
 * ============================================================ */
#include "../fs/fat16.c"

extern int printf(const char *fmt, ...);

/* libc dosya I/O (kernel stdint ile çakışmamak için elle bildirilir) */
extern void *fopen(const char *, const char *);
extern int   fseek(void *, long, int);
extern unsigned long fread(void *, unsigned long, unsigned long, void *);
extern int   fclose(void *);

/* screen.h saplamaları */
void screen_print(const char *s)   { (void)s; }
void screen_println(const char *s) { (void)s; }
void screen_putchar(char c)        { (void)c; }
void screen_print_int(int32_t v)   { (void)v; }
void screen_print_hex(uint32_t v)  { (void)v; }

/* ATA saplaması: imaj dosyasından sektör oku */
static void *g_img;
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    int n = count ? count : 256;
    if (fseek(g_img, (long)lba * 512, 0 /*SEEK_SET*/) != 0) return -1;
    if (fread(buf, 1, (unsigned long)n * 512, g_img) != (unsigned long)n * 512) return -1;
    return 0;
}

static int fails = 0;
#define CHK(c, m) do { if (!(c)) { printf("HATA: %s\n", (m)); fails++; } } while (0)

int main(int argc, char **argv) {
    if (argc != 3) { printf("kullanim: %s <img> <beklenen>\n", argv[0]); return 2; }

    g_img = fopen(argv[1], "rb");
    if (!g_img) { printf("HATA: imaj acilamadi\n"); return 2; }

    /* Beklenen içeriği oku */
    static char want[8192];
    void *ef = fopen(argv[2], "rb");
    if (!ef) { printf("HATA: beklenen dosya acilamadi\n"); return 2; }
    int wlen = (int)fread(want, 1, sizeof(want), ef);
    fclose(ef);

    CHK(fat16_mount() == 0, "fat16_mount basarisiz (gecerli FAT16 degil?)");

    static char buf[8192];
    int n = fat16_read("hello.txt", buf, sizeof(buf));   /* küçük harf */
    CHK(n == wlen && memcmp(buf, want, (n > 0 ? n : 0)) == 0,
        "HELLO.TXT icerigi (kucuk harf isim) eslesmiyor");

    n = fat16_read("HELLO.TXT", buf, sizeof(buf));        /* büyük harf */
    CHK(n == wlen && memcmp(buf, want, (n > 0 ? n : 0)) == 0,
        "HELLO.TXT icerigi (buyuk harf isim) eslesmiyor");

    CHK(fat16_read("yok.txt", buf, sizeof(buf)) < 0, "olmayan dosya bulundu?");

    fat16_list();   /* çökmeden çalışmalı (çıktı saplanmış) */
    fclose(g_img);

    if (fails) { printf("%d kontrol BASARISIZ\n", fails); return 1; }
    printf("OK: FAT16 gercek imajdan mount + HELLO.TXT okundu (%d bayt, "
           "kucuk/buyuk harf isim), olmayan dosya reddedildi\n", wlen);
    return 0;
}

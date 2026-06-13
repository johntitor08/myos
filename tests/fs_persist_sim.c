/* ============================================================
 * fs_persist_sim.c — kalıcı FS (fs_sync/fs_mount) uçtan uca testi
 *
 * Gerçek fs/fs.c'yi olduğu gibi derler (kopya mantık değil) ve ATA
 * sürücüsünü bellek-içi bir "disk" ile taklit ederek tam döngüyü test
 * eder: yaz -> sync -> (yeniden başlatma = RAM'i sıfırla) -> mount ->
 * oku ve içerikleri karşılaştır. Böylece QEMU olmadan da kalıcılık
 * mantığı (yerleşim, serileştirme, çok-bloklu dosyalar) doğrulanır.
 *
 * Donanıma dokunan tek kısım ata.c'nin PIO'sudur; o ayrı/var olan koddur.
 * Çıkış: 0 = tüm doğrulamalar geçti, !=0 = hata.
 * ============================================================ */
/* Gerçek FS kaynağını içeri al (static iç durumuyla birlikte). fs.c'nin
 * #define'ları (MAX_BLOCKS, DISK_DATA_LBA ...) ve kernel tipleri (uint*_t,
 * size_t) ile kstring.h yardımcıları (kstrlen) bu TU'da görünür olur.
 * Not: libc <stdint.h>'ı çekmemek için standart başlıklar dahil edilmez
 * (kernel'in include/stdint.h'ı ile çakışırdı); gereken tek libc işlevi
 * printf elle bildirilir. memcpy/memcmp memory.h'tan görünür, libc bağlar. */
#include "../fs/fs.c"

extern int printf(const char *fmt, ...);

/* ---- screen.h saplamaları (FS yalnız bunları çağırır) ---- */
void screen_print(const char *s)      { (void)s; }
void screen_println(const char *s)    { (void)s; }
void screen_print_int(int32_t v)      { (void)v; }

/* ---- ata.h saplamaları: bellek-içi disk ---- */
static unsigned char g_disk[(DISK_DATA_LBA + MAX_BLOCKS) * ATA_SECTOR_SIZE];

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    int n = count ? count : 256;
    if ((size_t)(lba + n) * ATA_SECTOR_SIZE > sizeof(g_disk)) return -1;
    memcpy(buf, g_disk + (size_t)lba * ATA_SECTOR_SIZE, (size_t)n * ATA_SECTOR_SIZE);
    return 0;
}
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    int n = count ? count : 256;
    if ((size_t)(lba + n) * ATA_SECTOR_SIZE > sizeof(g_disk)) return -1;
    memcpy(g_disk + (size_t)lba * ATA_SECTOR_SIZE, buf, (size_t)n * ATA_SECTOR_SIZE);
    return 0;
}

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("HATA: %s\n", (msg)); fails++; } } while (0)

int main(void) {
    /* 1) Boş disk: geçerli süperblok yok -> mount başarısız olmalı */
    CHECK(fs_mount() != 0, "bos diskte mount basarili dondu (sihir kontrolu yok?)");

    /* 2) FS'i kur ve test dosyaları yaz (biri çok-bloklu) */
    fs_init();

    const char *small = "Kalici dunya! 1234567890\n";
    CHECK(fs_write("alpha.txt", small, 0) >= 0, "alpha.txt yazilamadi");

    /* 1500 baytlık desen: 3 bloga yayilir (512+512+476) */
    static char big[1500];
    for (int i = 0; i < (int)sizeof(big); i++) big[i] = (char)(i * 7 + 3);
    CHECK(fs_write("big.bin", big, sizeof(big)) == (int)sizeof(big), "big.bin yazilamadi");

    /* 3) Diske kaydet */
    CHECK(fs_sync() == 0, "fs_sync basarisiz");

    /* 4) "Yeniden başlatma": RAM FS'ini sıfırla (fs_init örneklerle yeniden kurar,
     *    test dosyaları RAM'den silinir; yalnız diskte kalir). */
    fs_init();
    static char tmp[4096];
    CHECK(fs_read("alpha.txt", tmp, sizeof(tmp)) < 0, "reset sonrasi alpha.txt RAM'de hala var");

    /* 5) Diskten yükle */
    CHECK(fs_mount() == 0, "fs_mount basarisiz");

    /* 6) İçerikleri doğrula */
    int n = fs_read("alpha.txt", tmp, sizeof(tmp));
    CHECK(n == kstrlen(small) && memcmp(tmp, small, n) == 0,
          "alpha.txt icerigi mount sonrasi eslesmiyor");

    n = fs_read("big.bin", tmp, sizeof(tmp));
    CHECK(n == (int)sizeof(big) && memcmp(tmp, big, n) == 0,
          "big.bin (cok-bloklu) icerigi mount sonrasi eslesmiyor");

    /* Sync anındaki örnek dosya da kalıcı olmalı */
    CHECK(fs_read("readme.txt", tmp, sizeof(tmp)) > 0, "ornek readme.txt mount sonrasi yok");

    if (fails) { printf("%d kontrol BASARISIZ\n", fails); return 1; }
    printf("OK: kalici FS yaz->sync->reset->mount->oku dongusu dogrulandi "
           "(kucuk + cok-bloklu dosyalar, bos-disk reddi)\n");
    return 0;
}

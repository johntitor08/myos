/* ============================================================
 * boot_loader_sim.c — boot/boot.asm yükleyici doğrulaması
 *
 * QEMU olmadan da koşan host-tarafı regresyon testi: boot.asm'deki
 * LBA->CHS sektör-sektör yükleme mantığını birebir taklit eder,
 * gerçek myos.img'den okur, kerneli RAM'de yeniden kurar ve
 * kernel.bin ile bayt-bayt karşılaştırır.
 *
 * Kullanım:  boot_loader_sim <myos.img> <kernel.bin>
 * Çıkış: 0 = eşleşti, !=0 = hata.
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* boot.asm ile aynı 1.44MB floppy geometrisi */
#define SECTORS_PER_TRACK 18
#define NUM_HEADS         2
#define SECTOR_SIZE       512
#define FLOPPY_BYTES      (2880 * SECTOR_SIZE)
#define LOAD_PHYS         0x10000   /* boot.asm kerneli buraya yükler */

static unsigned char *slurp(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "acilamadi: %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = n;
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "kullanim: %s <myos.img> <kernel.bin>\n", argv[0]);
        return 2;
    }

    long img_len = 0, k_len = 0;
    unsigned char *img = slurp(argv[1], &img_len);
    unsigned char *kbin = slurp(argv[2], &k_len);
    if (!img || !kbin) return 2;

    /* KERNEL_SECTORS, Makefile'deki ile aynı formülle kernel.bin'den türer */
    int sectors = (int)((k_len + SECTOR_SIZE - 1) / SECTOR_SIZE);

    /* Yükleyiciyi taklit et: es=0x1000, bx=0 -> 0x10000; her sektörde es+=0x20 */
    static unsigned char mem[0x200000];
    memset(mem, 0xCC, sizeof mem);
    unsigned es = 0x1000;
    int lba = 1;                      /* boot sektörü LBA0; kernel LBA1'den */
    for (int i = 0; i < sectors; i++) {
        int sector = (lba % SECTORS_PER_TRACK) + 1;
        int head   = (lba / SECTORS_PER_TRACK) % NUM_HEADS;
        int cyl    = (lba / SECTORS_PER_TRACK) / NUM_HEADS;
        long off   = ((long)(cyl * NUM_HEADS + head) * SECTORS_PER_TRACK
                      + (sector - 1)) * SECTOR_SIZE;
        if (off + SECTOR_SIZE > img_len) {
            fprintf(stderr, "HATA: LBA %d imaj sinirini asti (off=%ld)\n", lba, off);
            return 1;
        }
        unsigned phys = es * 16; /* bx=0 */
        memcpy(mem + phys, img + off, SECTOR_SIZE);
        es += 0x20;
        lba++;
    }

    if (memcmp(mem + LOAD_PHYS, kbin, k_len) != 0) {
        fprintf(stderr, "HATA: 0x%X'te yeniden kurulan kernel kernel.bin ile eslesmiyor\n",
                LOAD_PHYS);
        return 1;
    }

    printf("OK: kernel=%ld bayt (%d sektor) 0x%X adresinde dogru yuklendi\n",
           k_len, sectors, LOAD_PHYS);
    free(img);
    free(kbin);
    return 0;
}

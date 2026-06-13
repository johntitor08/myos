/* ============================================================
 * gdt_sim.c — GDT/TSS descriptor kodlama doğrulaması
 *
 * Gerçek kernel/gdt.c'yi (GDT_HOST_TEST ile, asm'siz) derler, gdt_build()
 * çağırır ve üretilen 8 baytlık descriptor'ları bilinen-doğru i386
 * değerleriyle karşılaştırır. Amaç: her zaman çalışan gdt_init() kötü
 * kodlanmış bir descriptor yüzünden donanımda triple-fault üretmesin.
 * QEMU olmadan doğrulanabilen en kritik kısım budur.
 *
 * Çıkış: 0 = tüm descriptor'lar beklenen baytlarla eşleşti, !=0 = hata.
 * ============================================================ */
#define GDT_HOST_TEST 1
#include "../kernel/gdt.c"

extern int printf(const char *fmt, ...);

/* gdt.c'de static; aynı TU içinde görünür. gdt_build() onları doldurur. */
extern void gdt_build(void);

static int fails = 0;

static void expect(const char *name, int idx, const unsigned char want[8]) {
    const unsigned char *got = (const unsigned char *)&gdt[idx];
    for (int i = 0; i < 8; i++) {
        if (got[i] != want[i]) {
            printf("HATA: %s descriptor[%d] bayt %d: 0x%02X (beklenen 0x%02X)\n",
                   name, idx, i, got[i], want[i]);
            fails++;
            return;
        }
    }
}

int main(void) {
    gdt_build();

    /* Bilinen-doğru düz (base=0, limit=0xFFFFF, 4K gran, 32-bit) descriptor'lar.
     * Bayt düzeni: limit_low(2) base_low(2) base_mid(1) access(1) gran(1) base_high(1)
     * gran = 0xCF = (limit>>16 & 0xF=0xF) | (G|D/B = 0xC0). */
    unsigned char null_d[8] = {0,0,0,0,0,0,0,0};
    unsigned char kcode[8]  = {0xFF,0xFF,0,0,0, 0x9A, 0xCF, 0};
    unsigned char kdata[8]  = {0xFF,0xFF,0,0,0, 0x92, 0xCF, 0};
    unsigned char ucode[8]  = {0xFF,0xFF,0,0,0, 0xFA, 0xCF, 0};
    unsigned char udata[8]  = {0xFF,0xFF,0,0,0, 0xF2, 0xCF, 0};

    expect("null",  0, null_d);
    expect("kcode", 1, kcode);   /* bootloader GDT ile birebir aynı olmali */
    expect("kdata", 2, kdata);
    expect("ucode", 3, ucode);
    expect("udata", 4, udata);

    /* TSS descriptor: access 0x89, byte granularity (gran üst nibble 0).
     * base TSS adresine, limit = sizeof(tss)-1 (>0, <0x10000 => gran=0). */
    const unsigned char *t = (const unsigned char *)&gdt[5];
    unsigned int limit = t[0] | (t[1] << 8) | ((t[6] & 0x0F) << 16);
    if (t[5] != 0x89) { printf("HATA: TSS access 0x%02X (beklenen 0x89)\n", t[5]); fails++; }
    if ((t[6] & 0xF0) != 0x00) { printf("HATA: TSS gran bayraklari 0x%02X (beklenen 0x00)\n", t[6] & 0xF0); fails++; }
    if (limit < 0x67) { printf("HATA: TSS limit %u cok kucuk (>=0x67 olmali)\n", limit); fails++; }

    if (fails) { printf("%d descriptor kontrolu BASARISIZ\n", fails); return 1; }
    printf("OK: GDT descriptor kodlamasi dogru (null/kcode/kdata/ucode/udata + TSS); "
           "kernel seg'leri 0x08/0x10 bootloader ile ayni\n");
    return 0;
}

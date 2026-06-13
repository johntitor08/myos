/* ============================================================
 * elf_sim.c — gömülü userland ELF doğrulaması
 *
 * user/hello.elf'i okur ve çekirdeğin elf_load()'unun kabul kriterleriyle
 * (gerçek include/elf.h yapıları/sabitleri kullanılarak) doğrular: ET_EXEC
 * i386 ELF32, entry ve tüm PT_LOAD segmentleri [ELF_LOAD_MIN, ELF_LOAD_MAX)
 * penceresinde, dosya-içi aralıklar geçerli. Böylece 'run hello'nun ELF'i
 * doğru adrese yükleyeceği QEMU'suz doğrulanır.
 *
 * Bellek yazan elf_load() host'ta 0x500000'e yazıp segfault üretirdi; bu
 * yüzden yalnız (yazmayan) doğrulama mantığı tekrar uygulanır.
 *
 * Çıkış: 0 = geçerli ve yüklenebilir, !=0 = hata.
 * ============================================================ */
#include "../include/elf.h"

extern int printf(const char *fmt, ...);

/* libc'siz dosya okuma için minimal bildirimler */
typedef unsigned long size_t_h;
extern void *fopen(const char *, const char *);
extern size_t_h fread(void *, size_t_h, size_t_h, void *);
extern int fclose(void *);

int main(int argc, char **argv) {
    if (argc != 2) { printf("kullanim: %s <hello.elf>\n", argv[0]); return 2; }

    static unsigned char buf[65536];
    void *f = fopen(argv[1], "rb");
    if (!f) { printf("HATA: acilamadi: %s\n", argv[1]); return 2; }
    uint32_t size = (uint32_t)fread(buf, 1, sizeof(buf), f);
    fclose(f);

    int fails = 0;
    #define CHK(c, m) do { if (!(c)) { printf("HATA: %s\n", (m)); fails++; } } while (0)

    CHK(size >= sizeof(elf32_header_t), "dosya ELF basligindan kucuk");
    const elf32_header_t *h = (const elf32_header_t *)buf;
    CHK(h->magic == ELF_MAGIC, "ELF magic yanlis");
    CHK(h->bits == 1,          "32-bit degil");
    CHK(h->machine == EM_386,  "i386 degil");
    CHK(h->type == ET_EXEC,    "ET_EXEC degil");
    CHK(h->entry >= ELF_LOAD_MIN && h->entry < ELF_LOAD_MAX,
        "entry izinli yukleme penceresi disinda");
    CHK(h->ph_entry_size >= sizeof(elf32_phdr_t), "ph_entry_size kucuk");
    CHK(h->ph_count <= ELF_MAX_PHDRS, "cok fazla program header");

    int loads = 0;
    if (size >= sizeof(elf32_header_t) && h->ph_count <= ELF_MAX_PHDRS) {
        for (int i = 0; i < h->ph_count; i++) {
            const elf32_phdr_t *ph = (const elf32_phdr_t *)
                (buf + h->ph_offset + i * h->ph_entry_size);
            if (ph->type != PT_LOAD) continue;
            loads++;
            CHK(ph->offset <= size && ph->file_size <= size - ph->offset,
                "PT_LOAD kaynagi dosya disinda");
            CHK(ph->file_size <= ph->mem_size, "file_size > mem_size");
            CHK(ph->vaddr >= ELF_LOAD_MIN && ph->vaddr < ELF_LOAD_MAX &&
                ph->mem_size <= ELF_LOAD_MAX - ph->vaddr,
                "PT_LOAD hedefi izinli aralik disinda");
        }
    }
    CHK(loads >= 1, "PT_LOAD segmenti yok");

    if (fails) { printf("%d kontrol BASARISIZ\n", fails); return 1; }
    printf("OK: gomulu ELF gecerli ve yuklenebilir (ET_EXEC i386, entry 0x%X, "
           "%d PT_LOAD, hepsi 0x%X-0x%X penceresinde, %u bayt)\n",
           h->entry, loads, (unsigned)ELF_LOAD_MIN, (unsigned)ELF_LOAD_MAX, size);
    return 0;
}

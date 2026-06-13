#include "../include/fat16.h"
#include "../include/ata.h"
#include "../include/memory.h"
#include "../include/screen.h"

/* ============================================================
 * Salt-okunur FAT16 sürücüsü
 *
 * Yerleşim (sektör cinsinden):
 *   fat_start  = reserved
 *   root_start = reserved + num_fats * fat_size
 *   data_start = root_start + ceil(root_entries*32 / bps)   (= cluster 2)
 *   cluster N  -> data_start + (N-2) * sec_per_clus
 * FAT16 zinciri: sonraki = FAT[cluster] (2 bayt LE), >=0xFFF8 = zincir sonu.
 * ============================================================ */

#define FAT_SECTOR 512

static struct {
    int      mounted;
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint16_t reserved;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t fat_size;       /* sektör/FAT */
    uint32_t fat_start;      /* LBA */
    uint32_t root_start;     /* LBA */
    uint32_t root_sectors;
    uint32_t data_start;     /* cluster 2'nin LBA'sı */
} fat;

static int rd(uint32_t lba, void *buf) { return ata_read_sectors(lba, 1, buf); }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* "hello.txt" -> 11 baytlık 8.3 ("HELLO   TXT"), büyük harfe çevirir. */
static void to_83(const char *name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0;
    while (*name && *name != '.' && i < 8) {
        char c = *name++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[i++] = c;
    }
    while (*name && *name != '.') name++;      /* fazla taban karakterlerini atla */
    if (*name == '.') {
        name++;
        int j = 8;
        while (*name && j < 11) {
            char c = *name++;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            out[j++] = c;
        }
    }
}

int fat16_mount(void) {
    uint8_t s[FAT_SECTOR];
    fat.mounted = 0;
    if (rd(0, s) != 0) return -1;
    if (s[510] != 0x55 || s[511] != 0xAA) return -1;     /* boot imzası */

    uint16_t bps = rd16(s + 11);
    uint8_t  spc = s[13];
    uint16_t rsv = rd16(s + 14);
    uint8_t  nf  = s[16];
    uint16_t rec = rd16(s + 17);
    uint16_t ts16 = rd16(s + 19);
    uint16_t fsz = rd16(s + 22);
    uint32_t ts32 = rd32(s + 32);

    if (bps != FAT_SECTOR || spc == 0 || nf == 0 || fsz == 0) return -1;
    uint32_t total = ts16 ? ts16 : ts32;
    uint32_t root_sectors = ((uint32_t)rec * 32 + bps - 1) / bps;
    uint32_t root_start = (uint32_t)rsv + (uint32_t)nf * fsz;
    uint32_t data_start = root_start + root_sectors;
    if (total == 0 || data_start >= total) return -1;

    /* FAT türü küme sayısıyla belirlenir; yalnız FAT16'yı kabul et. */
    uint32_t clusters = (total - data_start) / spc;
    if (clusters < 4085 || clusters > 65524) return -1;

    fat.bytes_per_sec = bps; fat.sec_per_clus = spc; fat.reserved = rsv;
    fat.num_fats = nf; fat.root_entries = rec; fat.fat_size = fsz;
    fat.fat_start = rsv; fat.root_start = root_start;
    fat.root_sectors = root_sectors; fat.data_start = data_start;
    fat.mounted = 1;
    return 0;
}

/* Kök dizinde 8.3 ismi ara. Bulursa first_clus/size doldurur, 1 döner. */
static int find_entry(const char want[11], uint16_t *first_clus, uint32_t *size) {
    uint8_t sec[FAT_SECTOR];
    uint32_t eps = fat.bytes_per_sec / 32;
    for (uint32_t i = 0; i < fat.root_sectors; i++) {
        if (rd(fat.root_start + i, sec) != 0) return -1;
        for (uint32_t e = 0; e < eps; e++) {
            uint8_t *d = sec + e * 32;
            if (d[0] == 0x00) return 0;            /* daha fazla giriş yok */
            if (d[0] == 0xE5) continue;            /* silinmiş */
            if (d[11] == 0x0F) continue;           /* LFN */
            if (d[11] & 0x08) continue;            /* birim etiketi/dizin dışı */
            if (memcmp(d, want, 11) == 0) {
                *first_clus = rd16(d + 26);
                *size = rd32(d + 28);
                return 1;
            }
        }
    }
    return 0;
}

int fat16_read(const char *name, void *buf, uint32_t max) {
    if (!fat.mounted) return -1;
    char want[11];
    to_83(name, want);

    uint16_t clus = 0; uint32_t fsize = 0;
    if (find_entry(want, &clus, &fsize) != 1) return -1;

    uint32_t to_read = (fsize < max) ? fsize : max;
    uint32_t done = 0;
    uint8_t sec[FAT_SECTOR];
    uint8_t fatbuf[FAT_SECTOR];
    int32_t cached_fat_sec = -1;

    while (clus >= 2 && clus < 0xFFF8 && done < to_read) {
        uint32_t clus_lba = fat.data_start + (uint32_t)(clus - 2) * fat.sec_per_clus;
        for (uint32_t sc = 0; sc < fat.sec_per_clus && done < to_read; sc++) {
            if (rd(clus_lba + sc, sec) != 0) return -1;
            uint32_t n = to_read - done;
            if (n > FAT_SECTOR) n = FAT_SECTOR;
            memcpy((uint8_t *)buf + done, sec, n);
            done += n;
        }
        /* Sonraki cluster'ı FAT'tan al (2 bayt; bps çift olduğundan sektör
         * sınırını aşmaz). */
        uint32_t fat_off = (uint32_t)clus * 2;
        uint32_t fat_sec = fat.fat_start + fat_off / fat.bytes_per_sec;
        uint32_t fat_idx = fat_off % fat.bytes_per_sec;
        if ((int32_t)fat_sec != cached_fat_sec) {
            if (rd(fat_sec, fatbuf) != 0) return -1;
            cached_fat_sec = (int32_t)fat_sec;
        }
        clus = rd16(fatbuf + fat_idx);
    }
    return (int)done;
}

void fat16_list(void) {
    if (!fat.mounted) { screen_println("[FAT16] Disk bagli degil."); return; }
    uint8_t sec[FAT_SECTOR];
    uint32_t eps = fat.bytes_per_sec / 32;
    int count = 0;
    screen_println("FAT16 kok dizin:");
    for (uint32_t i = 0; i < fat.root_sectors; i++) {
        if (rd(fat.root_start + i, sec) != 0) return;
        for (uint32_t e = 0; e < eps; e++) {
            uint8_t *d = sec + e * 32;
            if (d[0] == 0x00) goto fin;
            if (d[0] == 0xE5 || d[11] == 0x0F || (d[11] & 0x08)) continue;
            char nm[13]; int p = 0;
            for (int k = 0; k < 8 && d[k] != ' '; k++) nm[p++] = (char)d[k];
            if (d[8] != ' ') {
                nm[p++] = '.';
                for (int k = 8; k < 11 && d[k] != ' '; k++) nm[p++] = (char)d[k];
            }
            nm[p] = 0;
            uint32_t fsize = rd32(d + 28);
            screen_print("  "); screen_print(nm); screen_print("\t");
            screen_print_int((int32_t)fsize); screen_println(" byte");
            count++;
        }
    }
fin:
    if (!count) screen_println("  (bos)");
}

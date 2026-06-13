#include "../include/fs.h"
#include "../include/memory.h"
#include "../include/screen.h"
#include "../include/kstring.h"
#include "../include/ata.h"
#include "../user/hello_elf.h"   /* gömülü ring-3 örnek programı (run hello) */
#include <stdint.h>

/* ============================================================
 * MyFS - Basit in-memory dosya sistemi
 * Sabit sayıda inode, block-tabanlı depolama
 * ============================================================ */

#define FS_MAGIC        0x4D594653  /* "MYFS" */
#define BLOCK_SIZE      512
#define MAX_BLOCKS      256
#define MAX_FILES       64
#define MAX_FILENAME    32
#define BLOCKS_PER_FILE 16

/* Inode yapısı */
typedef struct {
    char     name[MAX_FILENAME];
    uint32_t size;
    uint32_t blocks[BLOCKS_PER_FILE];
    uint8_t  block_count;
    uint8_t  is_dir;
    uint8_t  in_use;
} inode_t;

/* Filesystem durumu */
static uint8_t  fs_blocks[MAX_BLOCKS][BLOCK_SIZE];
static uint8_t  block_map[MAX_BLOCKS];      /* 0=boş, 1=dolu */
static inode_t  inodes[MAX_FILES];
static uint8_t  fs_initialized = 0;

/* ============================================================
 * Kalıcı depolama (ATA diskine kaydet/yükle)
 *
 * On-disk yerleşim (ATA primary master, LBA):
 *   [0]                      süperblok (sihir + sürüm + yerleşim)
 *   [1 .. INODE_SECTORS]     inode tablosu
 *   [BMAP_LBA]               block_map (1 sektör)
 *   [DATA_LBA .. +MAX_BLOCKS] veri blokları (yalnız kullanılanlar yazılır)
 *
 * Ham yapı dökümü kullanılır: OS kendi yazdığını okur. Süperblok sihir +
 * sürüm + yerleşim parametreleriyle uyumsuz/boş disklere karşı korur.
 * ============================================================ */
#define FS_DISK_MAGIC    0x4D594631u   /* "MYF1" */
#define FS_DISK_VERSION  1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t max_files;
    uint32_t max_blocks;
    uint32_t inode_lba;
    uint32_t bmap_lba;
    uint32_t data_lba;
} fs_superblock_t;

#define SECTORS_FOR(bytes)  (((bytes) + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE)

#define DISK_SB_LBA     0u
#define DISK_INODE_LBA  1u
#define INODE_SECTORS   SECTORS_FOR(sizeof(inodes))
#define DISK_BMAP_LBA   (DISK_INODE_LBA + INODE_SECTORS)
#define BMAP_SECTORS    SECTORS_FOR(sizeof(block_map))
#define DISK_DATA_LBA   (DISK_BMAP_LBA + BMAP_SECTORS)

/* Sektöre hizalı I/O tamponları (boyutu sektör katına yuvarlanmış).
 * ata_*_sectors count*512 bayt aktardığından kaynak/hedef tampon tam
 * o boyutta olmalı; bu yüzden bounce tampon üzerinden gidilir. */
static uint8_t sb_sector[ATA_SECTOR_SIZE];
static uint8_t bmap_sector[BMAP_SECTORS * ATA_SECTOR_SIZE];
static uint8_t inode_io[INODE_SECTORS * ATA_SECTOR_SIZE];

/* ============================================================
 * Boş block bul
 * ============================================================ */
static int find_free_block(void) {
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!block_map[i]) return i;
    }
    return -1;
}

/* ============================================================
 * Boş inode bul
 * ============================================================ */
static int find_free_inode(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodes[i].in_use) return i;
    }
    return -1;
}

/* ============================================================
 * İsimle dosya bul
 * ============================================================ */
static int find_inode_by_name(const char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodes[i].in_use) {
            int j = 0;
            while (inodes[i].name[j] && name[j] && inodes[i].name[j] == name[j]) j++;
            if (inodes[i].name[j] == 0 && name[j] == 0) return i;
        }
    }
    return -1;
}

/* String yardımcıları (kstrlen/kstrncpy) artık include/kstring.h'de. */

/* ============================================================
 * Dosya sistemini başlat
 * ============================================================ */
void fs_init(void) {
    memset(fs_blocks, 0, sizeof(fs_blocks));
    memset(block_map, 0, sizeof(block_map));
    memset(inodes, 0, sizeof(inodes));

    /* Kök dizini oluştur */
    inodes[0].in_use = 1;
    inodes[0].is_dir = 1;
    kstrncpy(inodes[0].name, "/", MAX_FILENAME);

    /* Örnek dosyalar oluştur */
    fs_write("readme.txt",
        "MyOS'a hosgeldiniz!\n"
        "Bu basit bir isletim sistemi.\n"
        "Shell'de 'help' yazarak komutlari gorebilirsiniz.\n", 0);

    fs_write("hello.txt", "Merhaba Dunya!\n", 0);

    /* Ring-3 örnek programı: 'run hello' ile kullanıcı modunda çalışır */
    fs_write("hello", (const char *)hello_elf, hello_elf_len);

    fs_initialized = 1;
    screen_println("[FS] MyFS hazir. Max dosya: 64, Block boyutu: 512B");
}

/* ============================================================
 * Dosya yaz (oluştur/üzerine yaz)
 * ============================================================ */
int fs_write(const char *name, const char *data, uint32_t size) {
    if (!size) size = (uint32_t)kstrlen(data);

    /* Varolan dosyayı bul veya yeni oluştur */
    int idx = find_inode_by_name(name);
    if (idx == -1) {
        idx = find_free_inode();
        if (idx == -1) { screen_println("[FS] Inode limiti doldu!"); return -1; }
        kstrncpy(inodes[idx].name, name, MAX_FILENAME);
        inodes[idx].in_use = 1;
        inodes[idx].is_dir = 0;
        inodes[idx].block_count = 0;
    } else {
        /* Eski block'ları serbest bırak */
        for (int i = 0; i < inodes[idx].block_count; i++) {
            block_map[inodes[idx].blocks[i]] = 0;
        }
        inodes[idx].block_count = 0;
    }

    /* Veriyi block'lara yaz */
    uint32_t written = 0;
    inodes[idx].size = size;

    while (written < size && inodes[idx].block_count < BLOCKS_PER_FILE) {
        int blk = find_free_block();
        if (blk == -1) { screen_println("[FS] Disk dolu!"); return -1; }

        uint32_t chunk = size - written;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;

        memcpy(fs_blocks[blk], data + written, chunk);
        block_map[blk] = 1;
        inodes[idx].blocks[inodes[idx].block_count++] = blk;
        written += chunk;
    }
    return (int)written;
}

/* ============================================================
 * Dosya oku
 * ============================================================ */
int fs_read(const char *name, char *buf, uint32_t max_size) {
    int idx = find_inode_by_name(name);
    if (idx == -1) return -1;

    uint32_t read = 0;
    for (int i = 0; i < inodes[idx].block_count && read < max_size; i++) {
        uint32_t chunk = inodes[idx].size - read;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
        if (chunk > max_size - read) chunk = max_size - read;
        memcpy(buf + read, fs_blocks[inodes[idx].blocks[i]], chunk);
        read += chunk;
    }
    return (int)read;
}

/* ============================================================
 * Dosya sil
 * ============================================================ */
int fs_delete(const char *name) {
    int idx = find_inode_by_name(name);
    if (idx == -1) return -1;

    for (int i = 0; i < inodes[idx].block_count; i++) {
        block_map[inodes[idx].blocks[i]] = 0;
    }
    memset(&inodes[idx], 0, sizeof(inode_t));
    return 0;
}

/* ============================================================
 * Dosya listesi
 * ============================================================ */
void fs_list(void) {
    int count = 0;
    screen_println("Dosyalar:");
    screen_println("--------");
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodes[i].in_use && !inodes[i].is_dir) {
            screen_print("  ");
            screen_print(inodes[i].name);
            screen_print("\t\t");
            screen_print_int((int32_t)inodes[i].size);
            screen_println(" byte");
            count++;
        }
    }
    if (!count) screen_println("  (bos)");
    screen_print("Toplam: ");
    screen_print_int(count);
    screen_println(" dosya");
}

/* ============================================================
 * Dosya var mı?
 * ============================================================ */
int fs_exists(const char *name) {
    return find_inode_by_name(name) != -1;
}

/* ============================================================
 * fs_sync — tüm FS durumunu ATA diskine yaz
 * Dönüş: 0 = başarılı, -1 = disk hatası.
 * ============================================================ */
int fs_sync(void) {
    /* Süperblok */
    memset(sb_sector, 0, sizeof(sb_sector));
    fs_superblock_t *sb = (fs_superblock_t *)sb_sector;
    sb->magic      = FS_DISK_MAGIC;
    sb->version    = FS_DISK_VERSION;
    sb->block_size = BLOCK_SIZE;
    sb->max_files  = MAX_FILES;
    sb->max_blocks = MAX_BLOCKS;
    sb->inode_lba  = DISK_INODE_LBA;
    sb->bmap_lba   = DISK_BMAP_LBA;
    sb->data_lba   = DISK_DATA_LBA;
    if (ata_write_sectors(DISK_SB_LBA, 1, sb_sector) != 0) return -1;

    /* Inode tablosu (sektöre hizalı bounce tampon) */
    memset(inode_io, 0, sizeof(inode_io));
    memcpy(inode_io, inodes, sizeof(inodes));
    if (ata_write_sectors(DISK_INODE_LBA, (uint8_t)INODE_SECTORS, inode_io) != 0) return -1;

    /* block_map */
    memset(bmap_sector, 0, sizeof(bmap_sector));
    memcpy(bmap_sector, block_map, sizeof(block_map));
    if (ata_write_sectors(DISK_BMAP_LBA, (uint8_t)BMAP_SECTORS, bmap_sector) != 0) return -1;

    /* Yalnız kullanılan veri bloklarını yaz */
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (block_map[i]) {
            if (ata_write_sectors(DISK_DATA_LBA + i, 1, fs_blocks[i]) != 0) return -1;
        }
    }
    return 0;
}

/* ============================================================
 * fs_mount — diskteki geçerli bir FS'yi belleğe yükle
 * Dönüş: 0 = yüklendi, -1 = geçerli/uyumlu FS yok ya da disk hatası.
 * ============================================================ */
int fs_mount(void) {
    if (ata_read_sectors(DISK_SB_LBA, 1, sb_sector) != 0) return -1;
    fs_superblock_t *sb = (fs_superblock_t *)sb_sector;
    if (sb->magic != FS_DISK_MAGIC || sb->version != FS_DISK_VERSION) return -1;
    /* Yerleşim uyumsuzsa (farklı sürümde derlenmiş disk) yükleme */
    if (sb->block_size != BLOCK_SIZE || sb->max_files != MAX_FILES ||
        sb->max_blocks != MAX_BLOCKS) return -1;

    if (ata_read_sectors(DISK_INODE_LBA, (uint8_t)INODE_SECTORS, inode_io) != 0) return -1;
    memcpy(inodes, inode_io, sizeof(inodes));

    if (ata_read_sectors(DISK_BMAP_LBA, (uint8_t)BMAP_SECTORS, bmap_sector) != 0) return -1;
    memcpy(block_map, bmap_sector, sizeof(block_map));

    memset(fs_blocks, 0, sizeof(fs_blocks));
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (block_map[i]) {
            if (ata_read_sectors(DISK_DATA_LBA + i, 1, fs_blocks[i]) != 0) return -1;
        }
    }
    fs_initialized = 1;
    return 0;
}

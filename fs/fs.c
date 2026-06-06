#include "../include/fs.h"
#include "../include/memory.h"
#include "../include/screen.h"
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

/* ============================================================
 * String kopyala (kernel kütüphanesiz)
 * ============================================================ */
static void kstrcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int kstrlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

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
    kstrcpy(inodes[0].name, "/", MAX_FILENAME);

    /* Örnek dosyalar oluştur */
    fs_write("readme.txt",
        "MyOS'a hosgeldiniz!\n"
        "Bu basit bir isletim sistemi.\n"
        "Shell'de 'help' yazarak komutlari gorebilirsiniz.\n", 0);

    fs_write("hello.txt", "Merhaba Dunya!\n", 0);

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
        kstrcpy(inodes[idx].name, name, MAX_FILENAME);
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

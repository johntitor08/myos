#include "../include/ata.h"
#include "../include/screen.h"

/* ============================================================
 * ATA PIO Modu Disk Sürücüsü
 * LBA28 adresleme, Primary bus, Master drive
 * ============================================================ */

static uint8_t  ata_available = 0;
static char     ata_model[41];
static uint32_t ata_sectors   = 0;

/* Port I/O */
static inline void outb(uint16_t p, uint8_t v)  { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  inb(uint16_t p) { uint8_t v;  __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }

/* ============================================================
 * Disk hazır olana kadar bekle
 * ============================================================ */
static int ata_wait_ready(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        uint8_t s = inb(ATA_PRIMARY_STATUS);
        if (s & ATA_SR_ERR)  return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRDY)) return 0;
    }
    return -2;  /* Timeout */
}

static int ata_wait_drq(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        uint8_t s = inb(ATA_PRIMARY_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -2;
}

/* ============================================================
 * ATA IDENTIFY — disk bilgilerini al
 * ============================================================ */
int ata_init(void) {
    /* Drive seç: Master (0xA0) */
    outb(ATA_PRIMARY_DRIVE, 0xA0);
    outb(ATA_PRIMARY_COUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MI, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);
    outb(ATA_PRIMARY_CMD, ATA_CMD_IDENTIFY);

    /* Status = 0 → disk yok */
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        screen_println("[ATA] Disk bulunamadi (QEMU HDD gerekli)");
        ata_available = 0;
        return -1;
    }

    /* BSY bitini bekle */
    if (ata_wait_drq() != 0) {
        screen_println("[ATA] IDENTIFY timeout");
        ata_available = 0;
        return -1;
    }

    /* 256 word oku */
    uint16_t identify[256];
    for (int i = 0; i < 256; i++)
        identify[i] = inw(ATA_PRIMARY_DATA);

    /* Model string (word 27-46, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        ata_model[i*2]   = (char)(identify[27+i] >> 8);
        ata_model[i*2+1] = (char)(identify[27+i] & 0xFF);
    }
    ata_model[40] = '\0';

    /* Toplam sektör sayısı (LBA28: word 60-61) */
    ata_sectors = ((uint32_t)identify[61] << 16) | identify[60];

    ata_available = 1;
    return 0;
}

void ata_print_info(void) {
    if (!ata_available) {
        screen_println("[ATA] Disk mevcut degil.");
        return;
    }
    screen_print("[ATA] Model: "); screen_println(ata_model);
    screen_print("[ATA] Kapasite: ");
    screen_print_int((int32_t)(ata_sectors / 2048));
    screen_println(" MB");
}

/* ============================================================
 * LBA28 ile sektör oku
 * ============================================================ */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (!ata_available) return -1;
    if (ata_wait_ready() != 0) return -1;

    outb(ATA_PRIMARY_DRIVE,  0xE0 | ((lba >> 24) & 0x0F));  /* LBA mode, Master */
    outb(ATA_PRIMARY_ERR,    0x00);
    outb(ATA_PRIMARY_COUNT,  count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba));
    outb(ATA_PRIMARY_LBA_MI, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_CMD,    ATA_CMD_READ_PIO);

    uint16_t *ptr = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++)
            ptr[s * 256 + i] = inw(ATA_PRIMARY_DATA);
    }
    return 0;
}

/* ============================================================
 * LBA28 ile sektör yaz
 * ============================================================ */
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (!ata_available) return -1;
    if (ata_wait_ready() != 0) return -1;

    outb(ATA_PRIMARY_DRIVE,  0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_ERR,    0x00);
    outb(ATA_PRIMARY_COUNT,  count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba));
    outb(ATA_PRIMARY_LBA_MI, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_CMD,    ATA_CMD_WRITE_PIO);

    const uint16_t *ptr = (const uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++)
            outw(ATA_PRIMARY_DATA, ptr[s * 256 + i]);
    }

    /* Cache flush */
    outb(ATA_PRIMARY_CMD, ATA_CMD_FLUSH);
    ata_wait_ready();
    return 0;
}

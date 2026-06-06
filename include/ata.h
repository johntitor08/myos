#ifndef ATA_H
#define ATA_H

#include "stdint.h"

#define ATA_SECTOR_SIZE     512

/* ATA I/O port adresleri (Primary bus) */
#define ATA_PRIMARY_DATA    0x1F0
#define ATA_PRIMARY_ERR     0x1F1
#define ATA_PRIMARY_COUNT   0x1F2
#define ATA_PRIMARY_LBA_LO  0x1F3
#define ATA_PRIMARY_LBA_MI  0x1F4
#define ATA_PRIMARY_LBA_HI  0x1F5
#define ATA_PRIMARY_DRIVE   0x1F6
#define ATA_PRIMARY_STATUS  0x1F7
#define ATA_PRIMARY_CMD     0x1F7

/* ATA komutları */
#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_FLUSH       0xE7

/* Status bitleri */
#define ATA_SR_BSY  0x80    /* Busy */
#define ATA_SR_DRDY 0x40    /* Drive ready */
#define ATA_SR_DRQ  0x08    /* Data request */
#define ATA_SR_ERR  0x01    /* Error */

/* Disk bölüm tablosu (MBR partition entry) */
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t lba_size;
} partition_entry_t;

/* Fonksiyonlar */
int  ata_init(void);
int  ata_read_sectors(uint32_t lba, uint8_t count, void *buf);
int  ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);
void ata_print_info(void);

#endif

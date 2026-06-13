#ifndef FAT16_H
#define FAT16_H

#include "stdint.h"

/* ============================================================
 * Salt-okunur FAT16 sürücüsü (ATA primary master).
 * Gerçek FAT16 imajlarını (mkfs.fat) okur: BPB doğrula, kök dizini tara,
 * 8.3 isimle dosya bul, FAT zincirini izleyerek oku.
 * ============================================================ */

/* BPB'yi diskten oku ve doğrula. 0=geçerli FAT16, -1=değil/hata. */
int  fat16_mount(void);

/* Kök dizini listele (isim + boyut). */
void fat16_list(void);

/* Dosyayı 8.3 ismiyle oku. Dönüş: okunan bayt, -1=yok/hata. */
int  fat16_read(const char *name, void *buf, uint32_t max);

#endif

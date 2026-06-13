/* ============================================================
 * rtl8139_rx_sim.c — net/net.c RX halka ofseti doğrulaması
 *
 * net_receive()'deki rx_offset ilerletme mantığını taklit eder ve
 * sarmalı (modulo RX_BUF_LEN) ofsetin her zaman tampon sınırları
 * içinde kaldığını doğrular. Sarma olmadan ofsetin tamponu aştığını
 * da gösterir (testin anlamlı olduğunu kanıtlar).
 *
 * Çıkış: 0 = değişmez korunuyor, !=0 = ihlal.
 * ============================================================ */
#include <stdio.h>
#include <stdint.h>

/* net.c ile aynı sabitler */
#define RX_BUF_LEN   8192
#define RX_BUF_SIZE  (RX_BUF_LEN + 16 + 1500)

#define PACKETS      5000
#define HDR_LEN      4          /* RTL8139 başına 4 baytlık [status,len] başlık */

int main(void) {
    uint16_t fixed = 0;   /* net.c'deki sarmalı ofset */
    uint16_t buggy = 0;   /* sarma olmayan eski davranış */
    int fixed_oob = 0, buggy_oob = 0;

    /* Değişen boyutlarda gelen paketleri simüle et */
    for (int i = 0; i < PACKETS; i++) {
        uint16_t pkt = (uint16_t)(64 + (i * 37) % 1457);   /* 64..1520 */

        /* Başlık, ilerletmeden ÖNCE mevcut ofsette okunur */
        if ((int)fixed + HDR_LEN > RX_BUF_SIZE) fixed_oob++;
        if ((int)buggy + HDR_LEN > RX_BUF_SIZE) buggy_oob++;

        fixed = (uint16_t)(((fixed + pkt + 4 + 3) & ~3) % RX_BUF_LEN);
        buggy = (uint16_t)((buggy + pkt + 4 + 3) & ~3);

        if (fixed >= RX_BUF_LEN) {
            fprintf(stderr, "HATA: sarmali ofset %u, RX_BUF_LEN %d sinirini asti\n",
                    fixed, RX_BUF_LEN);
            return 1;
        }
    }

    if (fixed_oob != 0) {
        fprintf(stderr, "HATA: sarmali ofset %d kez sinir disi baslik okudu\n", fixed_oob);
        return 1;
    }
    if (buggy_oob == 0) {
        fprintf(stderr, "HATA: test anlamsiz — sarma olmayan yol bile sinir disina cikmadi\n");
        return 1;
    }

    printf("OK: %d pakette sarmali ofset hep [0,%d) icinde (sinir disi=0); "
           "sarma olmayan eski yol %d kez sinir disi okurdu\n",
           PACKETS, RX_BUF_LEN, buggy_oob);
    return 0;
}

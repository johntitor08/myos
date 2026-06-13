/* ============================================================
 * arp_sim.c — arp_build_reply() doğrulaması
 *
 * Gerçek net/net.c'yi derler (donanım/ekran saplamalı) ve saf
 * arp_build_reply() fonksiyonunu test eder: bize yönelik bir ARP
 * isteğine doğru bir ARP yanıtı (alanlar + adresler) üretiyor mu, ve
 * bize-olmayan/ARP-olmayan/kısa çerçeveleri reddediyor mu. Böylece ping
 * için gereken ARP yolu QEMU olmadan doğrulanır.
 *
 * Çıkış: 0 = tüm kontroller geçti, !=0 = hata.
 * ============================================================ */
#include "../net/net.c"

extern int printf(const char *fmt, ...);

/* net.c'nin başvurduğu çekirdek sembolleri için saplamalar */
void screen_print(const char *s)   { (void)s; }
void screen_println(const char *s) { (void)s; }
void screen_putchar(char c)        { (void)c; }
void screen_print_hex(uint32_t v)  { (void)v; }
void screen_print_int(int32_t v)   { (void)v; }
void *kmalloc(size_t n)            { (void)n; return 0; }

static int fails = 0;
#define CHK(c, m) do { if (!(c)) { printf("HATA: %s\n", (m)); fails++; } } while (0)

/* 10.b.c.d -> ip_addr_t (octet0 en düşük baytta; net.c ile aynı düzen) */
static ip_addr_t mkip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (ip_addr_t)a | ((ip_addr_t)b << 8) | ((ip_addr_t)c << 16) | ((ip_addr_t)d << 24);
}

int main(void) {
    mac_addr_t my_mac  = {{0x52,0x54,0x00,0x12,0x34,0x56}};
    mac_addr_t req_mac = {{0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}};
    ip_addr_t  my_ip   = mkip(10,0,2,15);
    ip_addr_t  req_ip  = mkip(10,0,2,2);

    /* Bize yönelik geçerli ARP isteği kur */
    uint8_t req[ETH_HDR_LEN + sizeof(arp_packet_t)];
    eth_header_t *e = (eth_header_t *)req;
    arp_packet_t *a = (arp_packet_t *)(req + ETH_HDR_LEN);
    for (int i = 0; i < ETH_ADDR_LEN; i++) e->dst.bytes[i] = 0xFF;
    e->src  = req_mac;
    e->type = net_htons(ETH_TYPE_ARP);
    a->htype = net_htons(ARP_HTYPE_ETH);
    a->ptype = net_htons(ARP_PTYPE_IP);
    a->hlen  = ETH_ADDR_LEN; a->plen = 4;
    a->oper  = net_htons(ARP_OP_REQUEST);
    a->sha   = req_mac; a->spa = req_ip;
    for (int i = 0; i < ETH_ADDR_LEN; i++) a->tha.bytes[i] = 0x00;
    a->tpa   = my_ip;

    uint8_t out[64];
    int n = arp_build_reply(req, sizeof(req), my_mac, my_ip, out, sizeof(out));
    CHK(n == (int)(ETH_HDR_LEN + sizeof(arp_packet_t)), "yanit uzunlugu yanlis");

    if (n > 0) {
        eth_header_t *re = (eth_header_t *)out;
        arp_packet_t *ra = (arp_packet_t *)(out + ETH_HDR_LEN);
        CHK(memcmp(&re->dst, &req_mac, ETH_ADDR_LEN) == 0, "eth.dst isteyene degil");
        CHK(memcmp(&re->src, &my_mac,  ETH_ADDR_LEN) == 0, "eth.src bizim MAC degil");
        CHK(net_htons(re->type) == ETH_TYPE_ARP,           "eth.type ARP degil");
        CHK(net_htons(ra->oper) == ARP_OP_REPLY,           "oper REPLY degil");
        CHK(memcmp(&ra->sha, &my_mac, ETH_ADDR_LEN) == 0,  "arp.sha bizim MAC degil");
        CHK(ra->spa == my_ip,                              "arp.spa bizim IP degil");
        CHK(memcmp(&ra->tha, &req_mac, ETH_ADDR_LEN) == 0, "arp.tha isteyenin MAC'i degil");
        CHK(ra->tpa == req_ip,                             "arp.tpa isteyenin IP'si degil");
    }

    /* Negatif: bize-olmayan ARP isteği reddedilmeli */
    a->tpa = mkip(10,0,2,99);
    CHK(arp_build_reply(req, sizeof(req), my_mac, my_ip, out, sizeof(out)) < 0,
        "baska IP icin ARP istegi reddedilmedi");
    a->tpa = my_ip;

    /* Negatif: ARP olmayan ethertype */
    e->type = net_htons(ETH_TYPE_IP);
    CHK(arp_build_reply(req, sizeof(req), my_mac, my_ip, out, sizeof(out)) < 0,
        "ARP olmayan cerceve reddedilmedi");
    e->type = net_htons(ETH_TYPE_ARP);

    /* Negatif: çok kısa çerçeve */
    CHK(arp_build_reply(req, ETH_HDR_LEN + 4, my_mac, my_ip, out, sizeof(out)) < 0,
        "kisa cerceve reddedilmedi");

    if (fails) { printf("%d kontrol BASARISIZ\n", fails); return 1; }
    printf("OK: ARP yanit insasi dogru (alanlar+adresler) ve "
           "bize-olmayan/ARP-olmayan/kisa cerceveler reddedildi\n");
    return 0;
}

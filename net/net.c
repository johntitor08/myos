#include "../include/net.h"
#include "../include/screen.h"
#include "../include/memory.h"
#include "../include/io.h"
#include "../include/critical.h"
#include "../include/task.h"
#include "../include/timer.h"
#include "../include/critical.h"

/* ============================================================
 * MyOS Ağ Stack'i
 * - RTL8139 PCI ağ kartı sürücüsü (QEMU destekler)
 * - Ethernet II çerçeveleme
 * - IPv4 + ICMP + UDP
 * ============================================================ */

static net_state_t net = {0};

/* Port I/O artık include/io.h'de. */

/* RTL8139 register offset'leri */
#define RTL_MAC0        0x00
#define RTL_MAR0        0x08
#define RTL_TSD0        0x10   /* Tx status */
#define RTL_TSAD0       0x20   /* Tx start addr */
#define RTL_RBSTART     0x30   /* Rx buffer start */
#define RTL_CMD         0x37
#define RTL_CAPR        0x38   /* Current addr of packet read */
#define RTL_CBR         0x3A   /* Current buffer address */
#define RTL_IMR         0x3C   /* Interrupt mask */
#define RTL_ISR         0x3E   /* Interrupt status */
#define RTL_TCR         0x40   /* Tx config */
#define RTL_RCR         0x44   /* Rx config */
#define RTL_CONFIG1     0x52

#define RTL_CMD_RX_EN   0x08
#define RTL_CMD_TX_EN   0x04
#define RTL_CMD_RST     0x10

/* TX/RX buffer */
/* RX halkası: RCR RBLEN=00 => 8K mantıksal halka. WRAP biti (1<<7) açık
 * olduğundan kart, halka sonundaki paketi bölmeyip bitişik olarak +1500
 * baytlık taşma alanına yazar; bu yüzden tampon 8K+16+1500 ayrılır ama
 * okuma ofseti 8K halka boyuna göre sarılmalıdır. */
#define RX_BUF_LEN      8192
#define RX_BUF_SIZE     (RX_BUF_LEN + 16 + 1500)
#define TX_BUF_COUNT    4

static uint8_t *rx_buffer = 0;
static uint8_t *tx_buffers[TX_BUF_COUNT];
static uint8_t  tx_current = 0;
static uint16_t rx_offset  = 0;

/* Gelen ICMP echo reply durumu (ping komutu için; net_poll task'ı RX'te
 * doldurur, shell ping komutu yoklar). */
static volatile int       g_ping_got = 0;
static volatile uint16_t  g_ping_seq = 0;

/* ============================================================
 * ARP önbelleği + outbound çözümleme.
 * udp/ping artık hedef MAC'i ARP ile çözüp unicast gönderir; çözülemezse
 * broadcast'e düşer. Aynı subnet'teki hedef doğrudan, dışındaki gateway
 * üzerinden çözülür.
 * ============================================================ */
#define ARP_CACHE_SIZE 8
typedef struct { ip_addr_t ip; mac_addr_t mac; uint8_t valid; } arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static uint8_t     arp_cache_next = 0;

static void arp_cache_put(ip_addr_t ip, const uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac.bytes, mac, 6); return;
        }
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip; memcpy(arp_cache[i].mac.bytes, mac, 6);
            arp_cache[i].valid = 1; return;
        }
    arp_cache[arp_cache_next].ip = ip;                 /* hepsi dolu: değiştir */
    memcpy(arp_cache[arp_cache_next].mac.bytes, mac, 6);
    arp_cache[arp_cache_next].valid = 1;
    arp_cache_next = (uint8_t)((arp_cache_next + 1) % ARP_CACHE_SIZE);
}
static int arp_cache_get(ip_addr_t ip, mac_addr_t *out) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            *out = arp_cache[i].mac; return 1;
        }
    return 0;
}
static void arp_send_request(ip_addr_t target) {
    static uint8_t fr[42];
    memset(fr, 0, sizeof(fr));
    eth_header_t *eth = (eth_header_t *)fr;
    for (int i = 0; i < 6; i++) eth->dst.bytes[i] = 0xFF;     /* broadcast */
    eth->src  = net.mac;
    eth->type = net_htons(ETH_TYPE_ARP);
    uint8_t *o = fr + ETH_HDR_LEN;
    o[0]=0; o[1]=1; o[2]=0x08; o[3]=0x00; o[4]=6; o[5]=4; o[6]=0; o[7]=1; /* request */
    memcpy(o + 8,  net.mac.bytes, 6);   /* sha = bizim MAC */
    memcpy(o + 14, &net.ip, 4);         /* spa = bizim IP */
    memcpy(o + 24, &target, 4);         /* tpa = hedef IP (tha=0) */
    net_send_raw(fr, 42);
}
/* dst için L2 hedefini çöz. Bulursa 1 + mac; aksi halde 0. */
static int net_resolve(ip_addr_t dst, mac_addr_t *mac) {
    if (!net.available) return 0;
    ip_addr_t target = dst;
    if ((dst & net.netmask) != (net.ip & net.netmask))
        target = net.gateway;                 /* subnet dışı -> gateway */
    if (arp_cache_get(target, mac)) return 1;
    for (int tries = 0; tries < 3; tries++) {
        arp_send_request(target);
        for (int t = 0; t < 50; t++) {        /* ~0.5s yanıt bekle */
            net_receive();
            if (arp_cache_get(target, mac)) return 1;
            task_sleep(10);
        }
    }
    return 0;
}

/* ============================================================
 * PCI: RTL8139 bul (basit brute-force tarama)
 * ============================================================ */
static inline void pci_write(uint8_t bus, uint8_t slot, uint8_t func,
                              uint8_t off, uint32_t val) {
    uint32_t addr = 0x80000000 | ((uint32_t)bus<<16) | ((uint32_t)slot<<11)
                  | ((uint32_t)func<<8) | (off & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

static inline uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t addr = 0x80000000 | ((uint32_t)bus<<16) | ((uint32_t)slot<<11)
                  | ((uint32_t)func<<8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static uint32_t pci_find_rtl8139(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read((uint8_t)bus, slot, 0, 0);
            uint16_t vendor = id & 0xFFFF;
            uint16_t device = id >> 16;
            if (vendor == RTL8139_VENDOR && device == RTL8139_DEVICE) {
                /* BAR0: I/O space base */
                uint32_t bar0 = pci_read((uint8_t)bus, slot, 0, 0x10);
                /* PCI bus master etkinleştir */
                uint32_t cmd = pci_read((uint8_t)bus, slot, 0, 0x04);
                cmd |= 0x05;
                pci_write((uint8_t)bus, slot, 0, 0x04, cmd);
                return bar0 & ~0x3;  /* I/O base addr */
            }
        }
    }
    return 0;
}

/* ============================================================
 * Ağı başlat
 * ============================================================ */
int net_init(void) {
    uint32_t io = pci_find_rtl8139();
    if (!io) {
        screen_println("[NET] RTL8139 bulunamadi. QEMU: -nic rtl8139 ekleyin.");
        net.available = 0;
        return -1;
    }

    net.io_base   = io;
    net.available = 1;

    /* Power on */
    outb(io + RTL_CONFIG1, 0x00);

    /* Reset */
    outb(io + RTL_CMD, RTL_CMD_RST);
    uint32_t timeout = 100000;
    while ((inb(io + RTL_CMD) & RTL_CMD_RST) && timeout--);

    /* MAC adresini oku */
    for (int i = 0; i < 6; i++)
        net.mac.bytes[i] = inb(io + RTL_MAC0 + i);

    /* IP konfigürasyonu (sabit - DHCP yok) */
    net.ip      = (10)  | (0 << 8)   | (2 << 16)  | (15 << 24);
    net.gateway = (10)  | (0 << 8)   | (2 << 16)  | (2  << 24);
    net.netmask = (255) | (255 << 8) | (255 << 16) | (0  << 24);

    /* RX buffer */
    rx_buffer = (uint8_t *)kmalloc(RX_BUF_SIZE);
    if (!rx_buffer) { screen_println("[NET] RX buffer yok!"); return -1; }
    memset(rx_buffer, 0, RX_BUF_SIZE);

    /* TX buffer'ları */
    for (int i = 0; i < TX_BUF_COUNT; i++) {
        tx_buffers[i] = (uint8_t *)kmalloc(1536);
        memset(tx_buffers[i], 0, 1536);
    }

    /* RX buffer adresini RTL'ye ver */
    outl(io + RTL_RBSTART, (uint32_t)rx_buffer);

    /* IMR=0: NIC interrupt'larını KAPALI tut. Sürücü polling kullanıyor
     * (net_poll task'ı net_receive ile yokluyor) ve kayıtlı bir NIC IRQ
     * handler'ı YOK. Interrupt açık olsaydı, gelen ilk çerçevede NIC IRQ
     * yükseltir, handler ISR'yi (0x3E) temizlemediği için PIC EOI sonrası
     * hemen yeniden tetiklenir -> IRQ storm -> sistem kilitlenir. */
    outw(io + RTL_IMR, 0x0000);

    /* RCR: Accept All + wrap */
    outl(io + RTL_RCR, 0xF | (1 << 7));

    /* TX + RX etkinleştir */
    outb(io + RTL_CMD, RTL_CMD_RX_EN | RTL_CMD_TX_EN);

    screen_print("[NET] RTL8139 hazir. IO="); screen_print_hex(io);
    screen_print(" MAC=");
    for (int i = 0; i < 6; i++) {
        screen_print_hex(net.mac.bytes[i]);
        if (i < 5) screen_putchar(':');
    }
    screen_putchar('\n');
    return 0;
}

void net_print_info(void) {
    if (!net.available) { screen_println("[NET] Ag karti yok."); return; }
    char ip_str[16];
    ip_to_str(net.ip, ip_str);
    screen_print("[NET] IP: "); screen_println(ip_str);
    ip_to_str(net.gateway, ip_str);
    screen_print("[NET] GW: "); screen_println(ip_str);
}

/* ============================================================
 * Ham Ethernet çerçevesi gönder
 * ============================================================ */
int net_send_raw(const void *data, uint16_t len) {
    if (!net.available) return -1;
    if (len > 1536) return -1;          /* TX tampon boyutu (taşmayı önle) */
    uint32_t io = net.io_base;

    /* tx_current + TX register'ları birden çok task'tan (shell ping vs
     * net_poll yanıtları) çağrılabilir; kritik bölgede atomik tut. */
    uint32_t f = irq_save();
    uint8_t slot = tx_current;
    tx_current = (tx_current + 1) % TX_BUF_COUNT;
    memcpy(tx_buffers[slot], data, len);
    outl(io + RTL_TSAD0 + slot * 4, (uint32_t)tx_buffers[slot]);
    outl(io + RTL_TSD0  + slot * 4, len);
    irq_restore(f);
    return 0;
}

/* ============================================================
 * Yardımcılar
 * ============================================================ */
uint16_t net_htons(uint16_t v) { return ((v & 0xFF) << 8) | ((v >> 8) & 0xFF); }
uint32_t net_htonl(uint32_t v) {
    return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16)
         | (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
}

uint16_t ip_checksum(const void *data, uint32_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *ptr++; len -= 2; }
    if (len) sum += *(uint8_t *)ptr;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void ip_to_str(ip_addr_t ip, char *buf) {
    uint8_t *b = (uint8_t *)&ip;
    /* basit itoa */
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        if (i) buf[pos++] = '.';
        int n = b[i];
        if (n >= 100) { buf[pos++] = '0' + n/100; n %= 100; }
        if (n >= 10)  { buf[pos++] = '0' + n/10;  n %= 10;  }
        buf[pos++] = '0' + n;
    }
    buf[pos] = '\0';
}

/* ============================================================
 * UDP paketi gönder
 * ============================================================ */
int udp_send(ip_addr_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const void *data, uint16_t data_len) {
    if (!net.available) return -1;

    static uint8_t frame[ETH_FRAME_MAX];
    memset(frame, 0, sizeof(frame));

    eth_header_t *eth = (eth_header_t *)frame;
    ip_header_t  *ip  = (ip_header_t  *)(frame + ETH_HDR_LEN);
    udp_header_t *udp = (udp_header_t *)(frame + ETH_HDR_LEN + sizeof(ip_header_t));
    uint8_t      *payload = frame + ETH_HDR_LEN + sizeof(ip_header_t) + sizeof(udp_header_t);

    /* Ethernet header: hedef MAC'i ARP ile çöz (çözülemezse broadcast) */
    mac_addr_t dmac;
    if (net_resolve(dst_ip, &dmac)) eth->dst = dmac;
    else for (int i = 0; i < 6; i++) eth->dst.bytes[i] = 0xFF;
    eth->src  = net.mac;
    eth->type = net_htons(ETH_TYPE_IP);

    /* IP header */
    ip->version_ihl = 0x45;
    ip->ttl         = 64;
    ip->protocol    = IP_PROTO_UDP;
    ip->src         = net.ip;
    ip->dst         = dst_ip;
    uint16_t ip_len = sizeof(ip_header_t) + sizeof(udp_header_t) + data_len;
    ip->total_len   = net_htons(ip_len);
    ip->checksum    = ip_checksum(ip, sizeof(ip_header_t));

    /* UDP header */
    udp->src_port = net_htons(src_port);
    udp->dst_port = net_htons(dst_port);
    udp->length   = net_htons((uint16_t)(sizeof(udp_header_t) + data_len));
    udp->checksum = 0;

    /* Payload */
    memcpy(payload, data, data_len);

    uint16_t frame_len = (uint16_t)(ETH_HDR_LEN + ip_len);
    return net_send_raw(frame, frame_len);
}

/* ============================================================
 * ICMP echo (ping) yanıtı oluştur ve gönder.
 * req_* işaretçileri rx_buffer içine bakar; yanıt ayrı bir tampona
 * kurulur. icmp_len, ICMP başlığı + verisi (çağıran sınırlamış olmalı).
 * ============================================================ */
static void icmp_echo_reply(eth_header_t *req_eth, ip_header_t *req_ip,
                            icmp_header_t *req_icmp, uint16_t icmp_len) {
    if (!net.available) return;
    static uint8_t rb[ETH_FRAME_MAX];
    if ((uint32_t)ETH_HDR_LEN + sizeof(ip_header_t) + icmp_len > sizeof(rb)) return;
    memset(rb, 0, sizeof(rb));

    eth_header_t  *eth  = (eth_header_t *)rb;
    ip_header_t   *ip   = (ip_header_t *)(rb + ETH_HDR_LEN);
    icmp_header_t *icmp = (icmp_header_t *)(rb + ETH_HDR_LEN + sizeof(ip_header_t));

    /* Ethernet: gönderene geri */
    eth->dst  = req_eth->src;
    eth->src  = net.mac;
    eth->type = net_htons(ETH_TYPE_IP);

    /* IP: kaynak/hedef ters çevir */
    ip->version_ihl = 0x45;
    ip->ttl         = 64;
    ip->protocol    = IP_PROTO_ICMP;
    ip->src         = net.ip;
    ip->dst         = req_ip->src;
    uint16_t ip_len = (uint16_t)(sizeof(ip_header_t) + icmp_len);
    ip->total_len   = net_htons(ip_len);
    ip->checksum    = 0;
    ip->checksum    = ip_checksum(ip, sizeof(ip_header_t));

    /* ICMP: echo request -> echo reply (type 0); id/seq/veriyi koru */
    memcpy(icmp, req_icmp, icmp_len);
    icmp->type     = 0;
    icmp->checksum = 0;
    icmp->checksum = ip_checksum(icmp, icmp_len);

    net_send_raw(rb, (uint16_t)(ETH_HDR_LEN + ip_len));
}

/* ============================================================
 * ARP
 * ============================================================ */

/* Saf yardımcı: gelen çerçeve bize yönelik bir ARP isteğiyse yanıtı 'out'a
 * kurar ve uzunluğunu döndürür; değilse -1. Donanım/global kullanmaz, bu
 * yüzden host'ta test edilebilir. */
int arp_build_reply(const uint8_t *req, uint16_t req_len,
                    mac_addr_t my_mac, ip_addr_t my_ip,
                    uint8_t *out, uint16_t out_cap) {
    if (req_len < ETH_HDR_LEN + sizeof(arp_packet_t)) return -1;
    if (out_cap < ETH_HDR_LEN + sizeof(arp_packet_t)) return -1;

    const eth_header_t *req_eth = (const eth_header_t *)req;
    if (net_htons(req_eth->type) != ETH_TYPE_ARP) return -1;

    const arp_packet_t *req_arp = (const arp_packet_t *)(req + ETH_HDR_LEN);
    if (net_htons(req_arp->htype) != ARP_HTYPE_ETH) return -1;
    if (net_htons(req_arp->ptype) != ARP_PTYPE_IP)  return -1;
    if (net_htons(req_arp->oper)  != ARP_OP_REQUEST) return -1;
    if (req_arp->tpa != my_ip) return -1;            /* bize sorulmuyor */

    eth_header_t *eth = (eth_header_t *)out;
    arp_packet_t *arp = (arp_packet_t *)(out + ETH_HDR_LEN);

    eth->dst  = req_eth->src;          /* isteyene geri */
    eth->src  = my_mac;
    eth->type = net_htons(ETH_TYPE_ARP);

    arp->htype = net_htons(ARP_HTYPE_ETH);
    arp->ptype = net_htons(ARP_PTYPE_IP);
    arp->hlen  = ETH_ADDR_LEN;
    arp->plen  = 4;
    arp->oper  = net_htons(ARP_OP_REPLY);
    arp->sha   = my_mac;               /* bizim MAC */
    arp->spa   = my_ip;                /* bizim IP  */
    arp->tha   = req_arp->sha;         /* isteyenin MAC */
    arp->tpa   = req_arp->spa;         /* isteyenin IP  */

    return (int)(ETH_HDR_LEN + sizeof(arp_packet_t));
}

int arp_send_request(ip_addr_t target_ip) {
    if (!net.available) return -1;
    static uint8_t frame[ETH_HDR_LEN + sizeof(arp_packet_t)];
    eth_header_t *eth = (eth_header_t *)frame;
    arp_packet_t *arp = (arp_packet_t *)(frame + ETH_HDR_LEN);

    for (int i = 0; i < ETH_ADDR_LEN; i++) eth->dst.bytes[i] = 0xFF;  /* broadcast */
    eth->src  = net.mac;
    eth->type = net_htons(ETH_TYPE_ARP);

    arp->htype = net_htons(ARP_HTYPE_ETH);
    arp->ptype = net_htons(ARP_PTYPE_IP);
    arp->hlen  = ETH_ADDR_LEN;
    arp->plen  = 4;
    arp->oper  = net_htons(ARP_OP_REQUEST);
    arp->sha   = net.mac;
    arp->spa   = net.ip;
    for (int i = 0; i < ETH_ADDR_LEN; i++) arp->tha.bytes[i] = 0x00;
    arp->tpa   = target_ip;

    return net_send_raw(frame, sizeof(frame));
}

/* ============================================================
 * RX: gelen paketleri işle
 * ============================================================ */
void net_receive(void) {
    static volatile int busy = 0;
    if (!net.available) return;

    /* Reentrancy koruması: net_receive hem net_poll task'inden hem de
     * cmd_ping'in bekleme döngüsünden çağrılabilir. Aynı anda iki çağrı
     * RX ring'i (rx_offset/CAPR) bozardı; ikinci çağrı erken döner. */
    uint32_t f = irq_save();
    if (busy) { irq_restore(f); return; }
    busy = 1;
    irq_restore(f);

    uint32_t io = net.io_base;

    while (!(inb(io + RTL_CMD) & 0x01)) {  /* RX buffer boş değil */
        uint16_t *hdr = (uint16_t *)(rx_buffer + rx_offset);
        /* hdr[0] = status, hdr[1] = length */
        uint16_t status  = hdr[0];
        uint16_t pkt_len = hdr[1];
        if (!(status & 0x01)) break;          /* ROK yok: geçerli paket değil */
        if (pkt_len < 4 || pkt_len > 1520) break;

        uint8_t *pkt = (uint8_t *)(rx_buffer + rx_offset + 4);
        eth_header_t *eth = (eth_header_t *)pkt;

        /* ARP isteği bize ise yanıtla: host'un MAC'imizi çözmesini sağlar,
         * böylece ICMP echo (ping) gerçekten bize ulaşır. */
        if (pkt_len >= ETH_HDR_LEN && net_htons(eth->type) == ETH_TYPE_ARP) {
            static uint8_t arp_reply[ETH_HDR_LEN + sizeof(arp_packet_t)];
            int rlen = arp_build_reply(pkt, pkt_len, net.mac, net.ip,
                                       arp_reply, sizeof(arp_reply));
            if (rlen > 0) {
                screen_println("[NET] ARP istegi alindi, yanitlaniyor.");
                net_send_raw(arp_reply, (uint16_t)rlen);
            }
        }

        /* Başlık alanlarını okumadan önce pkt_len'in onları kapsadığını
         * doğrula (kısa/runt çerçeveler tampondan taşma okumasına yol açar). */
        if (pkt_len >= ETH_HDR_LEN && net_htons(eth->type) == ETH_TYPE_IP &&
            pkt_len >= ETH_HDR_LEN + sizeof(ip_header_t)) {
            ip_header_t *ip = (ip_header_t *)(pkt + ETH_HDR_LEN);
            if (ip->protocol == IP_PROTO_ICMP) {
                uint16_t ip_total = net_htons(ip->total_len);
                if (ip_total >= sizeof(ip_header_t) + sizeof(icmp_header_t) &&
                    pkt_len >= ETH_HDR_LEN + ip_total) {
                    icmp_header_t *icmp =
                        (icmp_header_t *)((uint8_t *)ip + sizeof(ip_header_t));
                    if (icmp->type == 8) {           /* echo request -> yanıtla */
                        char src_str[16];
                        ip_to_str(ip->src, src_str);
                        screen_print("[NET] ICMP echo istegi, kaynak: ");
                        screen_println(src_str);
                        icmp_echo_reply(eth, ip, icmp,
                                        (uint16_t)(ip_total - sizeof(ip_header_t)));
                    } else if (icmp->type == 0) {    /* echo reply -> ping komutu için kaydet */
                        g_ping_seq = net_htons(icmp->seq);
                        g_ping_got = 1;
                    }
                }
            }
            else if (ip->protocol == IP_PROTO_UDP) {     /* DHCP yanıtı (port 68) */
                uint16_t ip_total = net_htons(ip->total_len);
                if (ip_total >= sizeof(ip_header_t) + sizeof(udp_header_t) &&
                    pkt_len >= ETH_HDR_LEN + ip_total) {
                    udp_header_t *udp =
                        (udp_header_t *)((uint8_t *)ip + sizeof(ip_header_t));
                    uint16_t ulen = net_htons(udp->length);
                    if (net_htons(udp->dst_port) == DHCP_SPORT &&
                        ulen >= sizeof(udp_header_t) &&
                        (uint32_t)(ETH_HDR_LEN + sizeof(ip_header_t) + ulen) <= pkt_len) {
                        dhcp_input((uint8_t *)udp + sizeof(udp_header_t),
                                   (uint32_t)(ulen - sizeof(udp_header_t)));
                    }
                }
            }
        }

        /* Ofseti 4'e hizala ve 8K mantıksal halka içinde sar; sarmazsak
         * rx_offset tampon sonunu aşıp hdr[0]/hdr[1]'i sınır dışı okur ve
         * CAPR donanımla senkronizasyonunu kaybeder. */
        rx_offset = (uint16_t)(((rx_offset + pkt_len + 4 + 3) & ~3) % RX_BUF_LEN);
        outw(io + RTL_CAPR, (uint16_t)(rx_offset - 16));
    }

    busy = 0;
}

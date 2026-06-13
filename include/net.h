#ifndef NET_H
#define NET_H

#include "stdint.h"

/* Ethernet frame boyutları */
#define ETH_FRAME_MAX   1518
#define ETH_ADDR_LEN    6
#define ETH_HDR_LEN     14

/* EtherType */
#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_IP     0x0800

/* IP protokolleri */
#define IP_PROTO_ICMP   1
#define IP_PROTO_UDP    17
#define IP_PROTO_TCP    6

/* ARP */
#define ARP_HTYPE_ETH   1
#define ARP_PTYPE_IP    0x0800
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

/* RTL8139 PCI konfigürasyonu */
#define RTL8139_VENDOR  0x10EC
#define RTL8139_DEVICE  0x8139

/* Ethernet adresi */
typedef struct {
    uint8_t bytes[ETH_ADDR_LEN];
} __attribute__((packed)) mac_addr_t;

/* IP adresi */
typedef uint32_t ip_addr_t;

/* Ethernet header */
typedef struct __attribute__((packed)) {
    mac_addr_t dst;
    mac_addr_t src;
    uint16_t   type;
} eth_header_t;

/* IPv4 header */
typedef struct __attribute__((packed)) {
    uint8_t    version_ihl;
    uint8_t    tos;
    uint16_t   total_len;
    uint16_t   id;
    uint16_t   flags_frag;
    uint8_t    ttl;
    uint8_t    protocol;
    uint16_t   checksum;
    ip_addr_t  src;
    ip_addr_t  dst;
} ip_header_t;

/* UDP header */
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

/* ICMP header */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_header_t;

/* ARP paketi (Ethernet/IPv4). Adresler ip_addr_t ile aynı bayt düzeninde
 * (kablo = ağ sırası) tutulur; htype/ptype/oper ise net_htons gerektirir. */
typedef struct __attribute__((packed)) {
    uint16_t   htype;
    uint16_t   ptype;
    uint8_t    hlen;
    uint8_t    plen;
    uint16_t   oper;
    mac_addr_t sha;     /* gönderen MAC */
    ip_addr_t  spa;     /* gönderen IP  */
    mac_addr_t tha;     /* hedef MAC    */
    ip_addr_t  tpa;     /* hedef IP     */
} arp_packet_t;

/* Network state */
typedef struct {
    mac_addr_t mac;
    ip_addr_t  ip;
    ip_addr_t  gateway;
    ip_addr_t  netmask;
    uint32_t   io_base;
    uint8_t    available;
} net_state_t;

/* Fonksiyonlar */
int  net_init(void);
void net_print_info(void);
int  net_send_raw(const void *data, uint16_t len);
void net_receive(void);

/* Bize yönelik bir ARP isteğine yanıt çerçevesi kur (saf; host-testlenebilir).
 * Dönüş: yanıt uzunluğu, ya da -1 (ARP isteği değil / bize değil / yetersiz). */
int  arp_build_reply(const uint8_t *req, uint16_t req_len,
                     mac_addr_t my_mac, ip_addr_t my_ip,
                     uint8_t *out, uint16_t out_cap);
/* Bir IP için ARP isteği gönder (kablo üstünde MAC çözümü) */
int  arp_send_request(ip_addr_t target_ip);

/* Yardımcılar */
uint16_t net_htons(uint16_t v);
uint32_t net_htonl(uint32_t v);
uint16_t ip_checksum(const void *data, uint32_t len);

/* UDP gönder */
int udp_send(ip_addr_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const void *data, uint16_t len);

/* IP'yi string'e çevir */
void ip_to_str(ip_addr_t ip, char *buf);

#endif

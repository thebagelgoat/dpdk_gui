#include "module_base.h"
#include "../node_out.h"
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_cycles.h>
#include <rte_random.h>
#include <rte_byteorder.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

typedef enum { PKTGEN_UDP = 0, PKTGEN_TCP = 1, PKTGEN_ICMP = 2 } pktgen_proto_t;

typedef struct {
    uint64_t       rate_pps;
    uint64_t       hz;
    uint64_t       last_cycles;
    double         token_accum;

    uint32_t       src_net;
    uint32_t       src_host_range;
    uint32_t       dst_net;
    uint32_t       dst_host_range;

    uint8_t        src_mac[RTE_ETHER_ADDR_LEN];
    uint8_t        dst_mac[RTE_ETHER_ADDR_LEN];

    pktgen_proto_t proto;
    uint16_t       src_port_min, src_port_max;
    uint16_t       dst_port_min, dst_port_max;
    uint16_t       pkt_size;
} pkt_gen_cfg_t;

static int parse_cidr(const char *cidr, uint32_t *net_out, uint32_t *range_out) {
    char buf[64];
    strncpy(buf, cidr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *slash = strchr(buf, '/');
    int prefix = 32;
    if (slash) { *slash = '\0'; prefix = atoi(slash + 1); }
    struct in_addr addr;
    if (inet_pton(AF_INET, buf, &addr) != 1) return -1;
    *net_out   = ntohl(addr.s_addr);
    *range_out = (prefix >= 32) ? 1 : (1u << (32 - prefix));
    return 0;
}

static int parse_mac(const char *s, uint8_t *mac) {
    unsigned a, b, c, d, e, f;
    if (sscanf(s, "%02x:%02x:%02x:%02x:%02x:%02x", &a, &b, &c, &d, &e, &f) != 6) return -1;
    mac[0] = a; mac[1] = b; mac[2] = c; mac[3] = d; mac[4] = e; mac[5] = f;
    return 0;
}

static void *pkt_gen_parse(json_t *cfg) {
    pkt_gen_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    json_t *j;
    j = json_object_get(cfg, "rate_pps");
    c->rate_pps = j ? (uint64_t)json_integer_value(j) : 0;

    const char *src_ip = json_string_value(json_object_get(cfg, "src_ip"));
    const char *dst_ip = json_string_value(json_object_get(cfg, "dst_ip"));
    if (parse_cidr(src_ip ? src_ip : "10.0.0.1/32", &c->src_net, &c->src_host_range) < 0) {
        c->src_net = 0x0a000001; c->src_host_range = 1;
    }
    if (parse_cidr(dst_ip ? dst_ip : "192.168.1.1/32", &c->dst_net, &c->dst_host_range) < 0) {
        c->dst_net = 0xc0a80101; c->dst_host_range = 1;
    }

    const char *smac = json_string_value(json_object_get(cfg, "src_mac"));
    const char *dmac = json_string_value(json_object_get(cfg, "dst_mac"));
    if (!smac || parse_mac(smac, c->src_mac) < 0)
        memcpy(c->src_mac, "\x02\x00\x00\x00\x00\x01", 6);
    if (!dmac || parse_mac(dmac, c->dst_mac) < 0)
        memset(c->dst_mac, 0xff, 6);

    const char *proto_s = json_string_value(json_object_get(cfg, "protocol"));
    if (proto_s && !strcmp(proto_s, "tcp"))       c->proto = PKTGEN_TCP;
    else if (proto_s && !strcmp(proto_s, "icmp")) c->proto = PKTGEN_ICMP;
    else                                           c->proto = PKTGEN_UDP;

    j = json_object_get(cfg, "src_port_min"); c->src_port_min = j ? (uint16_t)json_integer_value(j) : 1024;
    j = json_object_get(cfg, "src_port_max"); c->src_port_max = j ? (uint16_t)json_integer_value(j) : 65535;
    j = json_object_get(cfg, "dst_port_min"); c->dst_port_min = j ? (uint16_t)json_integer_value(j) : 80;
    j = json_object_get(cfg, "dst_port_max"); c->dst_port_max = j ? (uint16_t)json_integer_value(j) : 80;
    if (c->src_port_max < c->src_port_min) c->src_port_max = c->src_port_min;
    if (c->dst_port_max < c->dst_port_min) c->dst_port_max = c->dst_port_min;

    j = json_object_get(cfg, "pkt_size");
    c->pkt_size = j ? (uint16_t)json_integer_value(j) : 64;
    if (c->pkt_size < 60) c->pkt_size = 60;

    return c;
}

static int pkt_gen_init(node_desc_t *node) {
    pkt_gen_cfg_t *c = node->module_cfg;
    c->hz          = rte_get_timer_hz();
    c->last_cycles = rte_get_timer_cycles();
    c->token_accum = 0.0;
    printf("pkt_gen: rate=%lu pps, pkt_size=%u, proto=%d\n",
           (unsigned long)c->rate_pps, c->pkt_size, (int)c->proto);
    return 0;
}

static int pkt_gen_process(node_desc_t *node) {
    pkt_gen_cfg_t *c = node->module_cfg;

    /* Token bucket */
    unsigned budget;
    if (c->rate_pps == 0) {
        budget = BURST_SIZE;
    } else {
        uint64_t now     = rte_get_timer_cycles();
        uint64_t elapsed = now - c->last_cycles;
        c->last_cycles   = now;
        c->token_accum  += (double)elapsed / (double)c->hz * (double)c->rate_pps;
        if (c->token_accum > (double)BURST_SIZE) c->token_accum = (double)BURST_SIZE;
        budget = (unsigned)c->token_accum;
        if (budget == 0) return 0;
        c->token_accum -= (double)budget;
    }

    uint16_t eth_len = RTE_ETHER_HDR_LEN;
    uint16_t ip_len  = (uint16_t)sizeof(struct rte_ipv4_hdr);
    uint16_t l4_len  = (c->proto == PKTGEN_TCP)  ? (uint16_t)sizeof(struct rte_tcp_hdr)
                     : (c->proto == PKTGEN_UDP)   ? (uint16_t)sizeof(struct rte_udp_hdr)
                     : 8u;
    uint16_t min_sz   = eth_len + ip_len + l4_len;
    uint16_t frame_sz = (c->pkt_size >= min_sz) ? c->pkt_size : min_sz;
    uint16_t payload_len = frame_sz - eth_len - ip_len - l4_len;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = 0;

    while (n < budget) {
        struct rte_mbuf *m = rte_pktmbuf_alloc(g_pktmbuf_pool);
        if (!m) break;

        char *pkt_data = rte_pktmbuf_append(m, frame_sz);
        if (!pkt_data) { rte_pktmbuf_free(m); break; }
        memset(pkt_data, 0, frame_sz);

        uint32_t src_ip = c->src_net + (uint32_t)(rte_rand() % (uint64_t)c->src_host_range);
        uint32_t dst_ip = c->dst_net + (uint32_t)(rte_rand() % (uint64_t)c->dst_host_range);

        uint16_t src_port = 0, dst_port = 0;
        if (c->proto != PKTGEN_ICMP) {
            src_port = c->src_port_min +
                (uint16_t)(rte_rand() % (uint64_t)(c->src_port_max - c->src_port_min + 1));
            dst_port = c->dst_port_min +
                (uint16_t)(rte_rand() % (uint64_t)(c->dst_port_max - c->dst_port_min + 1));
        }

        /* Ethernet header */
        struct rte_ether_hdr *eth = (struct rte_ether_hdr *)pkt_data;
        memcpy(eth->src_addr.addr_bytes, c->src_mac, RTE_ETHER_ADDR_LEN);
        memcpy(eth->dst_addr.addr_bytes, c->dst_mac, RTE_ETHER_ADDR_LEN);
        eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

        /* IPv4 header */
        struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
        ip->version_ihl   = 0x45;
        ip->total_length  = rte_cpu_to_be_16(frame_sz - eth_len);
        ip->packet_id     = rte_cpu_to_be_16((uint16_t)rte_rand());
        ip->time_to_live  = 64;
        ip->next_proto_id = (c->proto == PKTGEN_TCP) ? 6
                          : (c->proto == PKTGEN_UDP) ? 17 : 1;
        ip->src_addr      = rte_cpu_to_be_32(src_ip);
        ip->dst_addr      = rte_cpu_to_be_32(dst_ip);
        ip->hdr_checksum  = rte_ipv4_cksum(ip);

        /* L4 header */
        void *l4 = (ip + 1);
        if (c->proto == PKTGEN_UDP) {
            struct rte_udp_hdr *udp = (struct rte_udp_hdr *)l4;
            udp->src_port  = rte_cpu_to_be_16(src_port);
            udp->dst_port  = rte_cpu_to_be_16(dst_port);
            udp->dgram_len = rte_cpu_to_be_16(l4_len + payload_len);
        } else if (c->proto == PKTGEN_TCP) {
            struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)l4;
            tcp->src_port  = rte_cpu_to_be_16(src_port);
            tcp->dst_port  = rte_cpu_to_be_16(dst_port);
            tcp->sent_seq  = rte_cpu_to_be_32((uint32_t)rte_rand());
            tcp->data_off  = (uint8_t)((sizeof(struct rte_tcp_hdr) / 4) << 4);
            tcp->tcp_flags = 0x02;   /* SYN */
            tcp->rx_win    = rte_cpu_to_be_16(65535);
        }
        /* ICMP: zeroed 8-byte header = echo reply (type 0, code 0) */

        m->pkt_len = m->data_len = frame_sz;
        pkts[n++] = m;
    }

    if (n == 0) return 0;

    uint64_t bytes = 0;
    for (unsigned i = 0; i < n; i++) bytes += pkts[i]->pkt_len;
    unsigned sent = node_out(node, pkts, n);
    atomic_fetch_add_explicit(&node->pkts_processed,  sent,  memory_order_relaxed);
    atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
    return (int)n;
}

module_ops_t pkt_gen_ops = {
    .init         = pkt_gen_init,
    .process      = pkt_gen_process,
    .destroy      = NULL,
    .parse_config = pkt_gen_parse,
};

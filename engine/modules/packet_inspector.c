#include "module_base.h"
#include "../node_out.h"
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>

#define INSPECTOR_BUF_SIZE 256

typedef struct {
    uint64_t ts_us;
    uint32_t src_ip;    /* host byte order */
    uint32_t dst_ip;    /* host byte order */
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t eth_type;  /* outer EtherType */
    uint16_t pkt_len;
    uint8_t  ip_proto;
    uint8_t  tcp_flags;
    uint8_t  _pad[2];
} inspector_sample_t;

typedef struct {
    int                sample_every_n;
    int                sample_counter;
    inspector_sample_t buf[INSPECTOR_BUF_SIZE];
    uint32_t           write_idx;   /* next slot to write (wraps via %) */
    uint32_t           total_seen;  /* total samples ever captured */
    pthread_spinlock_t lock;
} inspector_cfg_t;

static void *inspector_parse(json_t *cfg) {
    inspector_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    json_t *j = json_object_get(cfg, "sample_every_n");
    c->sample_every_n = j ? (int)json_integer_value(j) : 1;
    if (c->sample_every_n < 1) c->sample_every_n = 1;
    return c;
}

static int inspector_init(node_desc_t *node) {
    inspector_cfg_t *c = node->module_cfg;
    pthread_spin_init(&c->lock, PTHREAD_PROCESS_PRIVATE);
    return 0;
}

static int inspector_process(node_desc_t *node) {
    inspector_cfg_t *c = node->module_cfg;
    int total = 0;

    for (int ri = 0; ri < node->n_inputs; ri++) {
        if (!node->input_rings[ri] || !node->input_rings[ri]->ring) continue;
        struct rte_mbuf *pkts[BURST_SIZE];
        unsigned n = rte_ring_dequeue_burst(node->input_rings[ri]->ring,
                                             (void **)pkts, BURST_SIZE, NULL);
        if (n == 0) continue;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t now_us = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;

        uint64_t bytes = 0;
        for (unsigned i = 0; i < n; i++) {
            bytes += pkts[i]->pkt_len;

            c->sample_counter++;
            if (c->sample_counter >= c->sample_every_n) {
                c->sample_counter = 0;

                inspector_sample_t s = {0};
                s.ts_us   = now_us;
                s.pkt_len = (uint16_t)(pkts[i]->pkt_len > 65535 ? 65535 : pkts[i]->pkt_len);

                struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkts[i], struct rte_ether_hdr *);
                s.eth_type = rte_be_to_cpu_16(eth->ether_type);

                /* Resolve inner EtherType past optional VLAN tag */
                uint16_t inner = s.eth_type;
                void *l3 = (uint8_t *)eth + sizeof(*eth);
                if (inner == RTE_ETHER_TYPE_VLAN) {
                    struct rte_vlan_hdr *vh = (struct rte_vlan_hdr *)l3;
                    inner = rte_be_to_cpu_16(vh->eth_proto);
                    l3 = (uint8_t *)l3 + sizeof(*vh);
                }

                if (inner == RTE_ETHER_TYPE_IPV4) {
                    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)l3;
                    s.src_ip   = ntohl(ip->src_addr);
                    s.dst_ip   = ntohl(ip->dst_addr);
                    s.ip_proto = ip->next_proto_id;
                    void *l4   = (uint8_t *)ip + (ip->version_ihl & 0x0f) * 4;
                    if (s.ip_proto == 6) {
                        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)l4;
                        s.src_port  = rte_be_to_cpu_16(tcp->src_port);
                        s.dst_port  = rte_be_to_cpu_16(tcp->dst_port);
                        s.tcp_flags = tcp->tcp_flags;
                    } else if (s.ip_proto == 17) {
                        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)l4;
                        s.src_port = rte_be_to_cpu_16(udp->src_port);
                        s.dst_port = rte_be_to_cpu_16(udp->dst_port);
                    }
                }

                pthread_spin_lock(&c->lock);
                c->buf[c->write_idx % INSPECTOR_BUF_SIZE] = s;
                c->write_idx++;
                c->total_seen++;
                pthread_spin_unlock(&c->lock);
            }
        }

        unsigned sent = node_out(node, pkts, n);
        atomic_fetch_add_explicit(&node->pkts_processed,  sent,  memory_order_relaxed);
        atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
        total += n;
    }
    return total;
}

static json_t *inspector_get_extra_stats(void *module_cfg) {
    inspector_cfg_t *c = (inspector_cfg_t *)module_cfg;

    pthread_spin_lock(&c->lock);
    uint32_t widx  = c->write_idx;
    uint32_t total = c->total_seen;
    /* Snapshot the ring buffer while holding the lock */
    inspector_sample_t snap[INSPECTOR_BUF_SIZE];
    uint32_t count = (total < INSPECTOR_BUF_SIZE) ? total : INSPECTOR_BUF_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t slot = (widx - count + i) % INSPECTOR_BUF_SIZE;
        snap[i] = c->buf[slot];
    }
    pthread_spin_unlock(&c->lock);

    /* Serialize oldest→newest */
    json_t *arr = json_array();
    for (uint32_t i = 0; i < count; i++) {
        inspector_sample_t *s = &snap[i];
        json_t *obj = json_object();
        json_object_set_new(obj, "ts_us",     json_integer((json_int_t)s->ts_us));
        json_object_set_new(obj, "src_ip",    json_integer(s->src_ip));
        json_object_set_new(obj, "dst_ip",    json_integer(s->dst_ip));
        json_object_set_new(obj, "src_port",  json_integer(s->src_port));
        json_object_set_new(obj, "dst_port",  json_integer(s->dst_port));
        json_object_set_new(obj, "eth_type",  json_integer(s->eth_type));
        json_object_set_new(obj, "pkt_len",   json_integer(s->pkt_len));
        json_object_set_new(obj, "ip_proto",  json_integer(s->ip_proto));
        json_object_set_new(obj, "tcp_flags", json_integer(s->tcp_flags));
        json_array_append_new(arr, obj);
    }
    return arr;
}

static void inspector_destroy(node_desc_t *node) {
    inspector_cfg_t *c = node->module_cfg;
    pthread_spin_destroy(&c->lock);
}

module_ops_t packet_inspector_ops = {
    .init            = inspector_init,
    .process         = inspector_process,
    .destroy         = inspector_destroy,
    .parse_config    = inspector_parse,
    .rule_count      = NULL,
    .get_extra_stats = inspector_get_extra_stats,
};

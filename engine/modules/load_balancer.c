#include "module_base.h"
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_ether.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum { LB_ROUND_ROBIN, LB_RSS } lb_mode_t;

typedef struct {
    lb_mode_t mode;
    int       output_count;
    uint32_t  rr_counter;  /* round-robin state */
} lb_cfg_t;

static void *lb_parse(json_t *cfg) {
    lb_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    const char *mode_s = json_string_value(json_object_get(cfg, "mode"));
    c->mode = (mode_s && !strcmp(mode_s, "rss")) ? LB_RSS : LB_ROUND_ROBIN;
    json_t *oc = json_object_get(cfg, "output_count");
    c->output_count = oc ? (int)json_integer_value(oc) : 2;
    if (c->output_count < 2) c->output_count = 2;
    if (c->output_count > MAX_OUTPUTS) c->output_count = MAX_OUTPUTS;
    return c;
}

/* Software 5-tuple hash for RSS mode */
static inline uint32_t flow_hash(struct rte_mbuf *m) {
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4)
        return 0;
    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
    uint32_t h = ip->src_addr ^ ip->dst_addr;
    uint8_t proto = ip->next_proto_id;
    if (proto == 6 || proto == 17) {
        size_t ihl = (ip->version_ihl & 0x0F) * 4;
        void *l4 = (uint8_t *)ip + ihl;
        if (proto == 6) {
            struct rte_tcp_hdr *tcp = l4;
            h ^= (uint32_t)tcp->src_port << 16 | tcp->dst_port;
        } else {
            struct rte_udp_hdr *udp = l4;
            h ^= (uint32_t)udp->src_port << 16 | udp->dst_port;
        }
    }
    /* Finalise with Knuth multiplicative hash */
    h = h * 2654435761u;
    return h;
}

static int lb_process(node_desc_t *node) {
    lb_cfg_t *c = node->module_cfg;
    if (!node->input_rings[0] || !node->input_rings[0]->ring) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = rte_ring_dequeue_burst(node->input_rings[0]->ring,
                                         (void **)pkts, BURST_SIZE, NULL);
    if (n == 0) return 0;

    uint64_t bytes = 0;
    unsigned processed = 0;

    for (unsigned i = 0; i < n; i++) {
        bytes += pkts[i]->pkt_len;

        int out;
        if (c->mode == LB_RSS) {
            out = (int)(flow_hash(pkts[i]) % (uint32_t)c->output_count);
        } else {
            out = (int)(c->rr_counter % (uint32_t)c->output_count);
            c->rr_counter++;
        }

        if (node->output_rings[out] && node->output_rings[out]->ring) {
            if (rte_ring_enqueue(node->output_rings[out]->ring, pkts[i]) < 0) {
                rte_pktmbuf_free(pkts[i]);
                atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
                continue;
            }
        } else {
            rte_pktmbuf_free(pkts[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
            continue;
        }
        processed++;
    }

    atomic_fetch_add_explicit(&node->pkts_processed,  processed, memory_order_relaxed);
    atomic_fetch_add_explicit(&node->bytes_processed, bytes,     memory_order_relaxed);
    return n;
}

module_ops_t load_balancer_ops = {
    .init         = NULL,
    .process      = lb_process,
    .destroy      = NULL,
    .parse_config = lb_parse,
};

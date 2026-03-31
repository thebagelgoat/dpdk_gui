#include "module_base.h"
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_PORT_RULES 128

typedef enum { PROTO_TCP=6, PROTO_UDP=17, PROTO_BOTH=0 } proto_t;
typedef enum { ACTION_PASS, ACTION_DROP } filter_action_t;

typedef struct {
    uint16_t        ports[MAX_PORT_RULES];
    int             n_ports;
    proto_t         protocol;
    filter_action_t action;  /* action on MATCH */
} port_filter_cfg_t;

static void *port_filter_parse(json_t *cfg) {
    port_filter_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    const char *proto_s = json_string_value(json_object_get(cfg, "protocol"));
    if (proto_s) {
        if (!strcmp(proto_s, "tcp"))  c->protocol = PROTO_TCP;
        else if (!strcmp(proto_s, "udp")) c->protocol = PROTO_UDP;
        else c->protocol = PROTO_BOTH;
    }

    const char *act = json_string_value(json_object_get(cfg, "action"));
    c->action = (act && !strcmp(act, "drop")) ? ACTION_DROP : ACTION_PASS;

    json_t *ports = json_object_get(cfg, "ports");
    if (ports && json_is_array(ports)) {
        size_t i; json_t *p;
        json_array_foreach(ports, i, p) {
            if (c->n_ports >= MAX_PORT_RULES) break;
            c->ports[c->n_ports++] = (uint16_t)json_integer_value(p);
        }
    }
    return c;
}

static inline int port_matches(port_filter_cfg_t *c, uint16_t src, uint16_t dst) {
    for (int i = 0; i < c->n_ports; i++) {
        if (c->ports[i] == src || c->ports[i] == dst) return 1;
    }
    return 0;
}

static int port_filter_process(node_desc_t *node) {
    port_filter_cfg_t *c = node->module_cfg;
    if (!node->input_rings[0]  || !node->input_rings[0]->ring)  return 0;
    if (!node->output_rings[0] || !node->output_rings[0]->ring) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = rte_ring_dequeue_burst(node->input_rings[0]->ring,
                                         (void **)pkts, BURST_SIZE, NULL);
    if (n == 0) return 0;

    struct rte_mbuf *pass[BURST_SIZE];
    unsigned n_pass = 0;
    uint64_t bytes = 0;

    for (unsigned i = 0; i < n; i++) {
        struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkts[i], struct rte_ether_hdr *);
        uint16_t etype = rte_be_to_cpu_16(eth->ether_type);
        int matched = 0;

        if (etype == RTE_ETHER_TYPE_IPV4) {
            struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
            uint8_t proto = ip->next_proto_id;
            int proto_ok = (c->protocol == PROTO_BOTH) ||
                           (c->protocol == PROTO_TCP && proto == 6) ||
                           (c->protocol == PROTO_UDP && proto == 17);

            if (proto_ok) {
                size_t ip_hdr_len = (ip->version_ihl & 0x0F) * 4;
                void *l4 = (uint8_t *)ip + ip_hdr_len;
                uint16_t src_port = 0, dst_port = 0;
                if (proto == 6) {
                    struct rte_tcp_hdr *tcp = l4;
                    src_port = rte_be_to_cpu_16(tcp->src_port);
                    dst_port = rte_be_to_cpu_16(tcp->dst_port);
                } else if (proto == 17) {
                    struct rte_udp_hdr *udp = l4;
                    src_port = rte_be_to_cpu_16(udp->src_port);
                    dst_port = rte_be_to_cpu_16(udp->dst_port);
                }
                matched = port_matches(c, src_port, dst_port);
            }
        }

        int should_pass = (c->action == ACTION_PASS) ? matched : !matched;
        if (should_pass) {
            bytes += pkts[i]->pkt_len;
            pass[n_pass++] = pkts[i];
        } else {
            rte_pktmbuf_free(pkts[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
        }
    }

    if (n_pass > 0) {
        unsigned enq = rte_ring_enqueue_burst(node->output_rings[0]->ring,
                                               (void **)pass, n_pass, NULL);
        for (unsigned i = enq; i < n_pass; i++) {
            rte_pktmbuf_free(pass[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
        }
        atomic_fetch_add_explicit(&node->pkts_processed,  enq,   memory_order_relaxed);
        atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
    }
    return n;
}

module_ops_t port_filter_ops = {
    .init         = NULL,
    .process      = port_filter_process,
    .destroy      = NULL,
    .parse_config = port_filter_parse,
};

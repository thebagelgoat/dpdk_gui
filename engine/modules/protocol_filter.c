#include "module_base.h"
#include "../node_out.h"
#include <rte_ether.h>
#include <rte_ip.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_PROTO_ENTRIES 64

typedef enum { ACTION_PASS, ACTION_DROP } filter_action_t;

typedef struct {
    uint8_t  ip_protos[MAX_PROTO_ENTRIES];   /* IP protocol numbers (e.g. 17=UDP, 6=TCP) */
    int      n_ip_protos;
    uint16_t ethertypes[MAX_PROTO_ENTRIES];  /* EtherType values (e.g. 0x0800=IPv4) */
    int      n_ethertypes;
    filter_action_t action;         /* action on MATCH */
    filter_action_t default_action; /* action when nothing matches */
} protocol_filter_cfg_t;

static void *protocol_filter_parse(json_t *cfg) {
    protocol_filter_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    const char *act = json_string_value(json_object_get(cfg, "action"));
    c->action = (act && !strcmp(act, "drop")) ? ACTION_DROP : ACTION_PASS;

    const char *def = json_string_value(json_object_get(cfg, "default_action"));
    c->default_action = (def && !strcmp(def, "drop")) ? ACTION_DROP : ACTION_PASS;

    /* ip_protos: array of integers e.g. [17, 6, 1] */
    json_t *ip_arr = json_object_get(cfg, "ip_protos");
    if (ip_arr && json_is_array(ip_arr)) {
        size_t i; json_t *v;
        json_array_foreach(ip_arr, i, v) {
            if (c->n_ip_protos >= MAX_PROTO_ENTRIES) break;
            c->ip_protos[c->n_ip_protos++] = (uint8_t)json_integer_value(v);
        }
    }

    /* ethertypes: array of integers e.g. [2048, 34525] (0x0800=IPv4, 0x86DD=IPv6) */
    json_t *eth_arr = json_object_get(cfg, "ethertypes");
    if (eth_arr && json_is_array(eth_arr)) {
        size_t i; json_t *v;
        json_array_foreach(eth_arr, i, v) {
            if (c->n_ethertypes >= MAX_PROTO_ENTRIES) break;
            c->ethertypes[c->n_ethertypes++] = (uint16_t)json_integer_value(v);
        }
    }

    return c;
}

static int protocol_filter_process(node_desc_t *node) {
    protocol_filter_cfg_t *c = node->module_cfg;
    int total = 0;
    for (int ri = 0; ri < node->n_inputs; ri++) {
        if (!node->input_rings[ri] || !node->input_rings[ri]->ring) continue;
        struct rte_mbuf *pkts[BURST_SIZE];
        unsigned n = rte_ring_dequeue_burst(node->input_rings[ri]->ring,
                                             (void **)pkts, BURST_SIZE, NULL);
        if (n == 0) continue;

        struct rte_mbuf *pass[BURST_SIZE];
        unsigned n_pass = 0;
        uint64_t bytes  = 0;

        for (unsigned i = 0; i < n; i++) {
            struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkts[i], struct rte_ether_hdr *);
            uint16_t etype = rte_be_to_cpu_16(eth->ether_type);

            uint16_t inner_etype = etype;
            void    *l3 = (uint8_t *)eth + sizeof(*eth);
            if (inner_etype == RTE_ETHER_TYPE_VLAN) {
                struct rte_vlan_hdr *vh = (struct rte_vlan_hdr *)l3;
                inner_etype = rte_be_to_cpu_16(vh->eth_proto);
                l3 = (uint8_t *)l3 + sizeof(*vh);
            }

            uint8_t ip_proto = 0;
            int has_ip = 0;
            if (inner_etype == RTE_ETHER_TYPE_IPV4) {
                struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)l3;
                ip_proto = ip->next_proto_id;
                has_ip = 1;
            }

            int matched = 0;
            for (int j = 0; j < c->n_ethertypes && !matched; j++) {
                if (c->ethertypes[j] == etype) matched = 1;
            }
            if (!matched && has_ip) {
                for (int j = 0; j < c->n_ip_protos && !matched; j++) {
                    if (c->ip_protos[j] == ip_proto) matched = 1;
                }
            }

            if (matched)
                atomic_fetch_add_explicit(&node->rule_hits[0], 1, memory_order_relaxed);
            filter_action_t verdict = matched ? c->action : c->default_action;
            if (verdict == ACTION_PASS) {
                bytes += pkts[i]->pkt_len;
                pass[n_pass++] = pkts[i];
            } else {
                rte_pktmbuf_free(pkts[i]);
                atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
            }
        }

        if (n_pass > 0) {
            unsigned enq = node_out(node, pass, n_pass);
            atomic_fetch_add_explicit(&node->pkts_processed,  enq,   memory_order_relaxed);
            atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
        }
        total += n;
    }
    return total;
}

static int protocol_filter_rule_count(void *cfg) { (void)cfg; return 1; }

module_ops_t protocol_filter_ops = {
    .init         = NULL,
    .process      = protocol_filter_process,
    .destroy      = NULL,
    .parse_config = protocol_filter_parse,
    .rule_count   = protocol_filter_rule_count,
};

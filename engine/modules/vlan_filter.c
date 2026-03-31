#include "module_base.h"
#include <rte_ether.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_VLAN_IDS 64

typedef enum { ACTION_PASS, ACTION_DROP } filter_action_t;

typedef struct {
    uint16_t        vlan_ids[MAX_VLAN_IDS];
    int             n_vlan_ids;
    filter_action_t action;      /* action on MATCH */
    int             strip_tag;   /* strip VLAN tag on matched pass */
} vlan_filter_cfg_t;

static void *vlan_filter_parse(json_t *cfg) {
    vlan_filter_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    const char *act = json_string_value(json_object_get(cfg, "action"));
    c->action = (act && !strcmp(act, "drop")) ? ACTION_DROP : ACTION_PASS;

    json_t *strip = json_object_get(cfg, "strip_tag");
    c->strip_tag = strip ? json_is_true(strip) : 0;

    json_t *ids = json_object_get(cfg, "vlan_ids");
    if (ids && json_is_array(ids)) {
        size_t i; json_t *v;
        json_array_foreach(ids, i, v) {
            if (c->n_vlan_ids >= MAX_VLAN_IDS) break;
            c->vlan_ids[c->n_vlan_ids++] = (uint16_t)json_integer_value(v);
        }
    }
    return c;
}

static inline int vlan_matches(vlan_filter_cfg_t *c, uint16_t vid) {
    for (int i = 0; i < c->n_vlan_ids; i++)
        if (c->vlan_ids[i] == vid) return 1;
    return 0;
}

static int vlan_filter_process(node_desc_t *node) {
    vlan_filter_cfg_t *c = node->module_cfg;
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
        uint16_t vid = 0;
        if (etype == RTE_ETHER_TYPE_VLAN) {
            struct rte_vlan_hdr *vhdr = (struct rte_vlan_hdr *)(eth + 1);
            vid = rte_be_to_cpu_16(vhdr->vlan_tci) & 0x0FFF;
            matched = vlan_matches(c, vid);
        }

        int should_pass;
        if (c->action == ACTION_PASS) {
            should_pass = matched;
        } else {
            /* action == DROP: pass non-matching, drop matching */
            should_pass = !matched;
        }

        if (should_pass) {
            if (matched && c->strip_tag) {
                /* Strip VLAN tag by moving ether src+dst over the VLAN header */
                rte_vlan_strip(pkts[i]);
            }
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

module_ops_t vlan_filter_ops = {
    .init         = NULL,
    .process      = vlan_filter_process,
    .destroy      = NULL,
    .parse_config = vlan_filter_parse,
};

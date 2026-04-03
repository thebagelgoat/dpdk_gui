#include "module_base.h"
#include <rte_mbuf.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    int output_count;  /* 2 to MAX_OUTPUTS */
} dup_cfg_t;

static void *dup_parse(json_t *cfg) {
    dup_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    json_t *oc = json_object_get(cfg, "output_count");
    c->output_count = oc ? (int)json_integer_value(oc) : 2;
    if (c->output_count < 2) c->output_count = 2;
    if (c->output_count > MAX_OUTPUTS) c->output_count = MAX_OUTPUTS;
    return c;
}

static int dup_process(node_desc_t *node) {
    dup_cfg_t *c = node->module_cfg;
    int total = 0;
    for (int ri = 0; ri < node->n_inputs; ri++) {
        if (!node->input_rings[ri] || !node->input_rings[ri]->ring) continue;
        struct rte_mbuf *pkts[BURST_SIZE];
        unsigned n = rte_ring_dequeue_burst(node->input_rings[ri]->ring,
                                             (void **)pkts, BURST_SIZE, NULL);
        if (n == 0) continue;

        uint64_t bytes = 0;
        unsigned processed = 0;

        for (unsigned i = 0; i < n; i++) {
            bytes += pkts[i]->pkt_len;

            if (node->output_rings[0] && node->output_rings[0]->ring) {
                if (rte_ring_enqueue(node->output_rings[0]->ring, pkts[i]) < 0) {
                    rte_pktmbuf_free(pkts[i]);
                    atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
                    continue;
                }
            }

            for (int o = 1; o < c->output_count; o++) {
                if (!node->output_rings[o] || !node->output_rings[o]->ring) continue;
                struct rte_mbuf *clone = rte_pktmbuf_copy(pkts[i], g_pktmbuf_pool, 0, UINT32_MAX);
                if (!clone) {
                    atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
                    continue;
                }
                if (rte_ring_enqueue(node->output_rings[o]->ring, clone) < 0) {
                    rte_pktmbuf_free(clone);
                    atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
                }
            }
            processed++;
        }

        atomic_fetch_add_explicit(&node->pkts_processed,  processed, memory_order_relaxed);
        atomic_fetch_add_explicit(&node->bytes_processed, bytes,     memory_order_relaxed);
        total += n;
    }
    return total;
}

module_ops_t duplicator_ops = {
    .init         = NULL,
    .process      = dup_process,
    .destroy      = NULL,
    .parse_config = dup_parse,
};

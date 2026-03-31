#include "module_base.h"
#include "../node_out.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char label[128];
    int  reset_on_read;  /* unused in C; reset is done from Python via IPC if needed */
} counter_cfg_t;

static void *counter_parse(json_t *cfg) {
    counter_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    const char *lbl = json_string_value(json_object_get(cfg, "label"));
    strncpy(c->label, lbl ? lbl : "counter", sizeof(c->label) - 1);
    json_t *ror = json_object_get(cfg, "reset_on_read");
    c->reset_on_read = ror ? json_is_true(ror) : 0;
    return c;
}

static int counter_process(node_desc_t *node) {
    if (!node->input_rings[0] || !node->input_rings[0]->ring) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = rte_ring_dequeue_burst(node->input_rings[0]->ring,
                                         (void **)pkts, BURST_SIZE, NULL);
    if (n == 0) return 0;

    uint64_t bytes = 0;
    for (unsigned i = 0; i < n; i++) bytes += pkts[i]->pkt_len;

    /* Sink mode: free packets without calling node_out() so pkts_dropped is not incremented */
    if (node->n_outputs == 0) {
        for (unsigned i = 0; i < n; i++) rte_pktmbuf_free(pkts[i]);
        atomic_fetch_add_explicit(&node->pkts_processed,  n,     memory_order_relaxed);
        atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
        return n;
    }

    unsigned processed = node_out(node, pkts, n);
    atomic_fetch_add_explicit(&node->pkts_processed,  processed, memory_order_relaxed);
    atomic_fetch_add_explicit(&node->bytes_processed, bytes,     memory_order_relaxed);
    return n;
}

module_ops_t counter_ops = {
    .init         = NULL,
    .process      = counter_process,
    .destroy      = NULL,
    .parse_config = counter_parse,
};

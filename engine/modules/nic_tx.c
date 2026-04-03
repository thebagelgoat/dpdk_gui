#include "module_base.h"
#include <rte_ethdev.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t port_id;
    uint16_t queue_id;
} nic_tx_cfg_t;

static void *nic_tx_parse(json_t *cfg) {
    nic_tx_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->port_id  = (uint16_t)json_integer_value(json_object_get(cfg, "port_id"));
    c->queue_id = (uint16_t)json_integer_value(json_object_get(cfg, "queue_id"));
    return c;
}

static int nic_tx_init(node_desc_t *node) {
    nic_tx_cfg_t *c = node->module_cfg;
    if (!rte_eth_dev_is_valid_port(c->port_id)) {
        fprintf(stderr, "NIC TX: port %u not available (NIC not bound to DPDK driver?) — node will idle\n", c->port_id);
    }
    return 0;
}

static int nic_tx_process(node_desc_t *node) {
    nic_tx_cfg_t *c = node->module_cfg;
    if (!rte_eth_dev_is_valid_port(c->port_id)) return 0;
    int total = 0;
    for (int ri = 0; ri < node->n_inputs; ri++) {
        if (!node->input_rings[ri] || !node->input_rings[ri]->ring) continue;
        struct rte_mbuf *pkts[BURST_SIZE];
        unsigned n = rte_ring_dequeue_burst(node->input_rings[ri]->ring,
                                             (void **)pkts, BURST_SIZE, NULL);
        if (n == 0) continue;

        uint64_t bytes = 0;
        for (unsigned i = 0; i < n; i++) bytes += pkts[i]->pkt_len;

        uint16_t sent = rte_eth_tx_burst(c->port_id, c->queue_id, pkts, (uint16_t)n);
        for (uint16_t i = sent; i < n; i++) {
            rte_pktmbuf_free(pkts[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
        }

        atomic_fetch_add_explicit(&node->pkts_processed,  sent,  memory_order_relaxed);
        atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
        total += n;
    }
    return total;
}

module_ops_t nic_tx_ops = {
    .init         = nic_tx_init,
    .process      = nic_tx_process,
    .destroy      = NULL,
    .parse_config = nic_tx_parse,
};

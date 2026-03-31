#include "module_base.h"
#include "../node_out.h"
#include <rte_ethdev.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t port_id;
    uint16_t queue_id;
    uint16_t burst_size;
} nic_rx_cfg_t;

static void *nic_rx_parse(json_t *cfg) {
    nic_rx_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->port_id   = (uint16_t)json_integer_value(json_object_get(cfg, "port_id"));
    c->queue_id  = (uint16_t)json_integer_value(json_object_get(cfg, "queue_id"));
    json_t *bs = json_object_get(cfg, "burst_size");
    c->burst_size = bs ? (uint16_t)json_integer_value(bs) : BURST_SIZE;
    if (c->burst_size < 1 || c->burst_size > 128) c->burst_size = BURST_SIZE;
    return c;
}

static int nic_rx_init(node_desc_t *node) {
    nic_rx_cfg_t *c = node->module_cfg;
    if (!rte_eth_dev_is_valid_port(c->port_id)) {
        fprintf(stderr, "NIC RX: port %u not available (NIC not bound to DPDK driver?) — node will idle\n", c->port_id);
    }
    return 0;
}

static int nic_rx_process(node_desc_t *node) {
    nic_rx_cfg_t *c = node->module_cfg;
    if (!rte_eth_dev_is_valid_port(c->port_id)) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    uint16_t n = rte_eth_rx_burst(c->port_id, c->queue_id, pkts, c->burst_size);
    if (n == 0) return 0;

    uint64_t bytes = 0;
    for (int i = 0; i < n; i++) bytes += pkts[i]->pkt_len;

    unsigned enq = node_out(node, pkts, n);

    atomic_fetch_add_explicit(&node->pkts_processed,  enq,   memory_order_relaxed);
    atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
    return n;
}

module_ops_t nic_rx_ops = {
    .init         = nic_rx_init,
    .process      = nic_rx_process,
    .destroy      = NULL,
    .parse_config = nic_rx_parse,
};

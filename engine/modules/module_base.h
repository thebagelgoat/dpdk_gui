#ifndef MODULE_BASE_H
#define MODULE_BASE_H

#include "../graph.h"

/*
 * Every module exports a module_ops_t instance.
 *
 * init()    — called once on the lcore before the main loop; return 0 on success
 * process() — hot path; return number of packets processed (0 = idle)
 * destroy() — called on graceful shutdown
 *
 * parse_config() — called on the main thread at startup to convert a jansson
 *                  object into a heap-allocated module_cfg struct.
 *                  The pipeline stores the pointer in node->module_cfg.
 */

#include <jansson.h>

typedef struct {
    int  (*init)(node_desc_t *node);
    int  (*process)(node_desc_t *node);
    void (*destroy)(node_desc_t *node);
    void *(*parse_config)(json_t *cfg_json);
} module_ops_t;

/* One entry per module type, indexed by module_type_t */
extern module_ops_t *module_registry[];

/* Convenience: look up ops for a node */
static inline module_ops_t *module_ops(node_desc_t *node) {
    return module_registry[node->type];
}

/* Declared in each module .c; collected in module_registry[] in graph.c */
extern module_ops_t nic_rx_ops;
extern module_ops_t nic_tx_ops;
extern module_ops_t ip_filter_ops;
extern module_ops_t vlan_filter_ops;
extern module_ops_t port_filter_ops;
extern module_ops_t duplicator_ops;
extern module_ops_t load_balancer_ops;
extern module_ops_t pcap_recorder_ops;
extern module_ops_t counter_ops;
extern module_ops_t template_ops;

#endif /* MODULE_BASE_H */

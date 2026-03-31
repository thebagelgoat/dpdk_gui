/*
 * TEMPLATE MODULE — copy this file to create a custom module.
 *
 * Steps to create a new module:
 *  1. Copy this file: cp template.c my_module.c
 *  2. Rename template_ops → my_module_ops
 *  3. Add "extern module_ops_t my_module_ops;" to module_base.h
 *  4. Add "MOD_MY_MODULE" to module_type_t in graph.h
 *  5. Add &my_module_ops to module_registry[] in graph.c
 *  6. Add parse mapping in module_type_from_string() in graph.c
 *  7. Add my_module.c to SRCS in the Makefile
 *  8. Add the module type to the frontend module palette
 */

#include "module_base.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char user_label[128];
    int  pass_through;
} template_cfg_t;

static void *template_parse(json_t *cfg) {
    template_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    const char *lbl = json_string_value(json_object_get(cfg, "user_label"));
    strncpy(c->user_label, lbl ? lbl : "custom", sizeof(c->user_label) - 1);
    json_t *pt = json_object_get(cfg, "pass_through");
    c->pass_through = pt ? json_is_true(pt) : 1;
    return c;
}

static int template_init(node_desc_t *node) {
    template_cfg_t *c = node->module_cfg;
    printf("Template module '%s' initialized (pass_through=%d)\n",
           c->user_label, c->pass_through);
    /* TODO: add your initialization logic here */
    return 0;
}

static int template_process(node_desc_t *node) {
    template_cfg_t *c = node->module_cfg;
    if (!node->input_rings[0] || !node->input_rings[0]->ring) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = rte_ring_dequeue_burst(node->input_rings[0]->ring,
                                         (void **)pkts, BURST_SIZE, NULL);
    if (n == 0) return 0;

    /* TODO: add your per-packet processing logic here */
    /* Example: this module simply counts and passes through */

    uint64_t bytes = 0;
    unsigned processed = 0;

    for (unsigned i = 0; i < n; i++) {
        bytes += pkts[i]->pkt_len;

        if (c->pass_through && node->output_rings[0] && node->output_rings[0]->ring) {
            if (rte_ring_enqueue(node->output_rings[0]->ring, pkts[i]) == 0) {
                processed++;
                continue;
            }
        }
        rte_pktmbuf_free(pkts[i]);
        atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
    }

    atomic_fetch_add_explicit(&node->pkts_processed,  processed, memory_order_relaxed);
    atomic_fetch_add_explicit(&node->bytes_processed, bytes,     memory_order_relaxed);
    return n;
}

static void template_destroy(node_desc_t *node) {
    template_cfg_t *c = node->module_cfg;
    printf("Template module '%s' destroyed\n", c->user_label);
    /* TODO: cleanup resources here */
}

module_ops_t template_ops = {
    .init         = template_init,
    .process      = template_process,
    .destroy      = template_destroy,
    .parse_config = template_parse,
};

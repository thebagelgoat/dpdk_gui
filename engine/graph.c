#include "graph.h"
#include "modules/module_base.h"
#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct rte_mempool *g_pktmbuf_pool = NULL;
volatile int        g_running      = 1;

module_ops_t *module_registry[] = {
    [MOD_NIC_RX]        = &nic_rx_ops,
    [MOD_NIC_TX]        = &nic_tx_ops,
    [MOD_IP_FILTER]     = &ip_filter_ops,
    [MOD_VLAN_FILTER]   = &vlan_filter_ops,
    [MOD_PORT_FILTER]      = &port_filter_ops,
    [MOD_PROTOCOL_FILTER]  = &protocol_filter_ops,
    [MOD_MAC_FILTER]       = &mac_filter_ops,
    [MOD_PCAP_RECORDER]    = &pcap_recorder_ops,
    [MOD_SPEEDOMETER]   = &counter_ops,
    [MOD_TEMPLATE]      = &template_ops,
};

module_type_t module_type_from_string(const char *s) {
    if (!strcmp(s, "nic_rx"))        return MOD_NIC_RX;
    if (!strcmp(s, "nic_tx"))        return MOD_NIC_TX;
    if (!strcmp(s, "ip_filter"))     return MOD_IP_FILTER;
    if (!strcmp(s, "vlan_filter"))   return MOD_VLAN_FILTER;
    if (!strcmp(s, "port_filter"))      return MOD_PORT_FILTER;
    if (!strcmp(s, "protocol_filter"))  return MOD_PROTOCOL_FILTER;
    if (!strcmp(s, "mac_filter"))       return MOD_MAC_FILTER;
    if (!strcmp(s, "pcap_recorder"))    return MOD_PCAP_RECORDER;
    if (!strcmp(s, "speedometer"))   return MOD_SPEEDOMETER;
    if (!strcmp(s, "template"))      return MOD_TEMPLATE;
    fprintf(stderr, "Unknown module type: %s\n", s);
    return MOD_TEMPLATE;
}

static output_mode_t parse_output_mode(const char *s) {
    if (!s)                          return OUT_FIRST;
    if (!strcmp(s, "duplicate"))     return OUT_DUPLICATE;
    if (!strcmp(s, "load_balance"))  return OUT_LOAD_BALANCE;
    return OUT_FIRST;
}

static lb_mode_t parse_lb_mode(const char *s) {
    if (s && !strcmp(s, "rss"))      return LB_RSS;
    return LB_ROUND_ROBIN;
}

int parse_graph(const char *path, pipeline_t *pipeline) {
    json_error_t err;
    json_t *root = json_load_file(path, 0, &err);
    if (!root) {
        fprintf(stderr, "JSON parse error in %s: %s (line %d)\n",
                path, err.text, err.line);
        return -1;
    }

    memset(pipeline, 0, sizeof(*pipeline));

    json_t *graph = json_object_get(root, "graph");
    if (!graph) { fprintf(stderr, "Missing 'graph' key\n"); json_decref(root); return -1; }

    /* ---- Parse nodes ---- */
    json_t *nodes_arr = json_object_get(graph, "nodes");
    if (!nodes_arr || !json_is_array(nodes_arr)) {
        fprintf(stderr, "Missing or invalid 'nodes' array\n"); json_decref(root); return -1;
    }

    size_t i;
    json_t *jnode;
    json_array_foreach(nodes_arr, i, jnode) {
        if (i >= MAX_NODES) { fprintf(stderr, "Too many nodes (max %d)\n", MAX_NODES); break; }

        node_desc_t *n = &pipeline->nodes[i];
        n->node_idx = (int)i;

        const char *id     = json_string_value(json_object_get(jnode, "id"));
        const char *type_s = json_string_value(json_object_get(jnode, "type"));
        const char *label  = json_string_value(json_object_get(jnode, "label"));

        if (!id || !type_s) { fprintf(stderr, "Node %zu missing id or type\n", i); continue; }

        strncpy(n->id,    id,               MAX_ID_LEN - 1);
        strncpy(n->label, label ? label : id, MAX_LABEL_LEN - 1);
        n->type = module_type_from_string(type_s);

        json_t *core_j = json_object_get(jnode, "core");
        n->core_id = core_j ? (int)json_integer_value(core_j) : 1;

        /* Parse output_mode and lb_mode from node config */
        json_t *cfg_j = json_object_get(jnode, "config");
        if (cfg_j) {
            const char *om = json_string_value(json_object_get(cfg_j, "output_mode"));
            const char *lm = json_string_value(json_object_get(cfg_j, "lb_mode"));
            n->output_mode = parse_output_mode(om);
            n->lb_mode     = parse_lb_mode(lm);
        }

        /* Parse module-specific config */
        if (cfg_j && module_registry[n->type]->parse_config) {
            n->module_cfg = module_registry[n->type]->parse_config(cfg_j);
            if (!n->module_cfg) {
                fprintf(stderr, "Failed to parse config for node %s\n", id);
                json_decref(root);
                return -1;
            }
            /* Set up rule hit counter slots */
            if (module_registry[n->type]->rule_count)
                n->n_rule_counters = module_registry[n->type]->rule_count(n->module_cfg);
        }

        pipeline->n_nodes++;
    }

    /* ---- Parse edges (create rings) ---- */
    json_t *edges_arr = json_object_get(graph, "edges");
    if (!edges_arr || !json_is_array(edges_arr)) {
        fprintf(stderr, "Missing or invalid 'edges' array\n"); json_decref(root); return -1;
    }

    json_t *jedge;
    json_array_foreach(edges_arr, i, jedge) {
        if (i >= MAX_EDGES) { fprintf(stderr, "Too many edges (max %d)\n", MAX_EDGES); break; }

        ring_desc_t *e = &pipeline->edges[i];
        e->edge_idx = (int)i;

        const char *src_id = json_string_value(json_object_get(jedge, "source"));
        const char *tgt_id = json_string_value(json_object_get(jedge, "target"));
        int src_port = (int)json_integer_value(json_object_get(jedge, "source_port"));
        int tgt_port = (int)json_integer_value(json_object_get(jedge, "target_port"));
        (void)src_port; /* kept for ring name generation; output wiring uses n_outputs order */

        if (!src_id || !tgt_id) { fprintf(stderr, "Edge %zu missing source/target\n", i); continue; }

        /* Ring metadata */
        json_t *ring_j    = json_object_get(jedge, "ring");
        const char *ring_name = ring_j ? json_string_value(json_object_get(ring_j, "name")) : NULL;
        uint32_t ring_size    = ring_j ? (uint32_t)json_integer_value(json_object_get(ring_j, "size")) : 1024;
        if (!ring_name) {
            snprintf(e->name, MAX_RING_NAME, "ring_%s_%d_%s_%d", src_id, src_port, tgt_id, tgt_port);
        } else {
            strncpy(e->name, ring_name, MAX_RING_NAME - 1);
        }
        e->size = ring_size;

        /* Create the rte_ring with a short index-based name (32-char limit) */
        char rte_name[32];
        snprintf(rte_name, sizeof(rte_name), "ring_%d", (int)i);
        e->ring = rte_ring_create(rte_name, e->size, SOCKET_ID_ANY, RING_F_SC_DEQ | RING_F_SP_ENQ);
        if (!e->ring) {
            fprintf(stderr, "Failed to create ring '%s' (rte name '%s') size %u\n",
                    e->name, rte_name, e->size);
            json_decref(root);
            return -1;
        }

        /* Wire source node output and target node input.
           Output rings are assigned in edge-declaration order so that multiple
           edges from the same source handle each get their own slot (0, 1, 2…).
           Input rings still use tgt_port for explicit multi-input nodes. */
        for (int j = 0; j < pipeline->n_nodes; j++) {
            node_desc_t *n = &pipeline->nodes[j];
            if (!strcmp(n->id, src_id)) {
                if (n->n_outputs < MAX_OUTPUTS) {
                    n->output_rings[n->n_outputs++] = e;
                }
            }
            if (!strcmp(n->id, tgt_id)) {
                if (tgt_port < MAX_OUTPUTS) {
                    n->input_rings[tgt_port] = e;
                    if (tgt_port >= n->n_inputs) n->n_inputs = tgt_port + 1;
                }
            }
        }

        pipeline->n_edges++;
    }

    json_decref(root);
    return 0;
}

void *get_module_ops(module_type_t type) {
    return module_registry[type];
}

void pipeline_free(pipeline_t *pipeline) {
    for (int i = 0; i < pipeline->n_nodes; i++) {
        node_desc_t *n = &pipeline->nodes[i];
        if (n->module_cfg) {
            free(n->module_cfg);
            n->module_cfg = NULL;
        }
    }
}

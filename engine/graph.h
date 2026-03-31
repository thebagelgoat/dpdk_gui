#ifndef GRAPH_H
#define GRAPH_H

#include <stdint.h>
#include <stdatomic.h>
#include <rte_ring.h>
#include <rte_mbuf.h>

#define MAX_NODES        64
#define MAX_EDGES        256
#define MAX_OUTPUTS      4
#define MAX_RING_NAME    64
#define MAX_ID_LEN       64
#define MAX_LABEL_LEN    128
#define BURST_SIZE       32

typedef enum {
    MOD_NIC_RX,
    MOD_NIC_TX,
    MOD_IP_FILTER,
    MOD_VLAN_FILTER,
    MOD_PORT_FILTER,
    MOD_DUPLICATOR,
    MOD_LOAD_BALANCER,
    MOD_PCAP_RECORDER,
    MOD_COUNTER,
    MOD_TEMPLATE
} module_type_t;

/* Per-edge ring descriptor */
typedef struct {
    char             name[MAX_RING_NAME];
    uint32_t         size;
    struct rte_ring *ring;
    int              edge_idx;
} ring_desc_t;

/* Per-node descriptor */
typedef struct node_desc {
    char            id[MAX_ID_LEN];
    char            label[MAX_LABEL_LEN];
    module_type_t   type;
    int             core_id;
    int             node_idx;

    /* ring connectivity */
    int             n_inputs;
    int             n_outputs;
    ring_desc_t    *input_rings[MAX_OUTPUTS];
    ring_desc_t    *output_rings[MAX_OUTPUTS];

    /* module-specific config (heap allocated) */
    void           *module_cfg;

    /* per-node stats (updated by worker) */
    _Atomic uint64_t pkts_processed;
    _Atomic uint64_t pkts_dropped;
    _Atomic uint64_t bytes_processed;

    /* busy/idle counters for utilization */
    _Atomic uint64_t busy_loops;
    _Atomic uint64_t idle_loops;
} node_desc_t;

/* Full pipeline */
typedef struct {
    node_desc_t  nodes[MAX_NODES];
    int          n_nodes;
    ring_desc_t  edges[MAX_EDGES];
    int          n_edges;
} pipeline_t;

/* Shared mbuf pool (set in main.c) */
extern struct rte_mempool *g_pktmbuf_pool;

/* Global running flag */
extern volatile int g_running;

/* Parse graph.json into pipeline_t. Returns 0 on success. */
int parse_graph(const char *path, pipeline_t *pipeline);

/* Free all heap-allocated module configs */
void pipeline_free(pipeline_t *pipeline);

/* Map type string to enum */
module_type_t module_type_from_string(const char *s);

#endif /* GRAPH_H */

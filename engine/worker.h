#ifndef WORKER_H
#define WORKER_H

#include "graph.h"

#define MAX_NODES_PER_LCORE 8

/* Context passed to each lcore worker */
typedef struct {
    node_desc_t *nodes[MAX_NODES_PER_LCORE];
    int          n_nodes;
    int          core_id;
} lcore_ctx_t;

/* Build per-lcore contexts from pipeline (groups nodes by core_id) */
int  worker_build_contexts(pipeline_t *pipeline, lcore_ctx_t *ctxs, int max_ctxs, int *n_ctxs);

/* lcore entry point (passed to rte_eal_remote_launch) */
int  worker_lcore_func(void *arg);

#endif /* WORKER_H */

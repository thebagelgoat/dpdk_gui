#include "worker.h"
#include "modules/module_base.h"
#include <string.h>
#include <stdio.h>
#include <rte_lcore.h>

int worker_build_contexts(pipeline_t *pipeline, lcore_ctx_t *ctxs, int max_ctxs, int *n_ctxs) {
    memset(ctxs, 0, sizeof(lcore_ctx_t) * max_ctxs);
    *n_ctxs = 0;

    for (int i = 0; i < pipeline->n_nodes; i++) {
        node_desc_t *n = &pipeline->nodes[i];
        int core = n->core_id;

        /* Find or create context for this core */
        lcore_ctx_t *ctx = NULL;
        for (int c = 0; c < *n_ctxs; c++) {
            if (ctxs[c].core_id == core) { ctx = &ctxs[c]; break; }
        }
        if (!ctx) {
            if (*n_ctxs >= max_ctxs) {
                fprintf(stderr, "Too many distinct cores requested\n");
                return -1;
            }
            ctx = &ctxs[*n_ctxs];
            ctx->core_id = core;
            (*n_ctxs)++;
        }

        if (ctx->n_nodes >= MAX_NODES_PER_LCORE) {
            fprintf(stderr, "Too many nodes on core %d\n", core);
            return -1;
        }
        ctx->nodes[ctx->n_nodes++] = n;
    }

    return 0;
}

int worker_lcore_func(void *arg) {
    lcore_ctx_t *ctx = (lcore_ctx_t *)arg;

    /* Initialize all modules on this lcore */
    for (int i = 0; i < ctx->n_nodes; i++) {
        node_desc_t *n = ctx->nodes[i];
        module_ops_t *ops = module_ops(n);
        if (ops->init) {
            if (ops->init(n) != 0) {
                fprintf(stderr, "Module init failed for node %s\n", n->id);
                g_running = 0;
                return -1;
            }
        }
    }

    /* Hot loop */
    while (g_running) {
        for (int i = 0; i < ctx->n_nodes; i++) {
            node_desc_t *n = ctx->nodes[i];
            int processed = module_ops(n)->process(n);

            if (processed > 0) {
                atomic_fetch_add_explicit(&n->busy_loops, 1, memory_order_relaxed);
            } else {
                atomic_fetch_add_explicit(&n->idle_loops, 1, memory_order_relaxed);
            }
        }
    }

    /* Cleanup */
    for (int i = 0; i < ctx->n_nodes; i++) {
        node_desc_t *n = ctx->nodes[i];
        module_ops_t *ops = module_ops(n);
        if (ops->destroy) ops->destroy(n);
    }

    return 0;
}

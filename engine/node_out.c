#include "node_out.h"
#include <rte_ring.h>
#include <rte_mbuf.h>
#include <stdatomic.h>
#include <stdint.h>

unsigned node_out(node_desc_t *node, struct rte_mbuf **pkts, unsigned n) {
    if (n == 0) return 0;

    switch (node->output_mode) {

    /* ---- OUT_FIRST (default) ------------------------------------------- */
    default:
    case OUT_FIRST: {
        if (!node->output_rings[0] || !node->output_rings[0]->ring) {
            for (unsigned i = 0; i < n; i++) rte_pktmbuf_free(pkts[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, n, memory_order_relaxed);
            return 0;
        }
        unsigned enq = rte_ring_enqueue_burst(node->output_rings[0]->ring,
                                               (void **)pkts, n, NULL);
        for (unsigned i = enq; i < n; i++) {
            rte_pktmbuf_free(pkts[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
        }
        return enq;
    }

    /* ---- OUT_DUPLICATE -------------------------------------------------- */
    case OUT_DUPLICATE: {
        /* Collect connected output rings */
        struct rte_ring *out[MAX_OUTPUTS];
        int n_out = 0;
        for (int o = 0; o < node->n_outputs; o++) {
            if (node->output_rings[o] && node->output_rings[o]->ring)
                out[n_out++] = node->output_rings[o]->ring;
        }
        if (n_out == 0) {
            for (unsigned i = 0; i < n; i++) rte_pktmbuf_free(pkts[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, n, memory_order_relaxed);
            return 0;
        }

        unsigned forwarded = 0;
        for (unsigned i = 0; i < n; i++) {
            int sent = 0;
            for (int o = 0; o < n_out; o++) {
                struct rte_mbuf *m;
                if (o == 0) {
                    m = pkts[i];  /* first output gets original */
                } else {
                    m = rte_pktmbuf_copy(pkts[i], g_pktmbuf_pool, 0, UINT32_MAX);
                    if (!m) continue;  /* skip this output if clone fails */
                }
                if (rte_ring_enqueue(out[o], m) < 0) {
                    rte_pktmbuf_free(m);
                    if (o == 0) {
                        /* Original failed — free remaining and drop */
                        atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
                        goto next_pkt;
                    }
                } else {
                    if (o == 0) sent = 1;
                }
            }
            if (sent) forwarded++;
        next_pkt:;
        }
        return forwarded;
    }

    /* ---- OUT_LOAD_BALANCE ----------------------------------------------- */
    case OUT_LOAD_BALANCE: {
        struct rte_ring *out[MAX_OUTPUTS];
        int n_out = 0;
        for (int o = 0; o < node->n_outputs; o++) {
            if (node->output_rings[o] && node->output_rings[o]->ring)
                out[n_out++] = node->output_rings[o]->ring;
        }
        if (n_out == 0) {
            for (unsigned i = 0; i < n; i++) rte_pktmbuf_free(pkts[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, n, memory_order_relaxed);
            return 0;
        }

        unsigned forwarded = 0;
        for (unsigned i = 0; i < n; i++) {
            int idx;
            if (node->lb_mode == LB_RSS) {
                uint32_t hash = pkts[i]->hash.rss
                              ? pkts[i]->hash.rss
                              : (uint32_t)(uintptr_t)pkts[i];
                idx = (int)(hash % (uint32_t)n_out);
            } else {
                idx = (int)(node->lb_rr_counter++ % (uint32_t)n_out);
            }
            if (rte_ring_enqueue(out[idx], pkts[i]) < 0) {
                rte_pktmbuf_free(pkts[i]);
                atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
            } else {
                forwarded++;
            }
        }
        return forwarded;
    }

    } /* switch */
}

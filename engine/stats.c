#include "stats.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <rte_ring.h>
#include <rte_lcore.h>

stats_snapshot_t g_latest_stats;

static pthread_t       s_thread;
static volatile int    s_running = 0;
static pipeline_t     *s_pipeline = NULL;

/* Previous values for delta calculations */
static uint64_t s_prev_pkts[MAX_STAT_NODES];
static uint64_t s_prev_busy[MAX_STAT_NODES];
static uint64_t s_prev_idle[MAX_STAT_NODES];

void stats_init(pipeline_t *pipeline) {
    memset(&g_latest_stats, 0, sizeof(g_latest_stats));
    memset(s_prev_pkts, 0, sizeof(s_prev_pkts));
    memset(s_prev_busy, 0, sizeof(s_prev_busy));
    memset(s_prev_idle, 0, sizeof(s_prev_idle));
    s_pipeline = pipeline;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double wall_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void stats_collect(pipeline_t *pipeline, stats_snapshot_t *snap) {
    static double last_time = 0;
    double now = now_sec();
    double dt = (last_time > 0) ? (now - last_time) : 1.0;
    if (dt < 0.001) dt = 0.001;
    last_time = now;

    snap->n_nodes = pipeline->n_nodes;
    snap->n_rings = pipeline->n_edges;
    snap->timestamp = wall_sec();
    memset(snap->lcore_util, 0, sizeof(snap->lcore_util));

    /* Node stats */
    for (int i = 0; i < pipeline->n_nodes; i++) {
        node_desc_t *n = &pipeline->nodes[i];
        node_stats_snap_t *s = &snap->nodes[i];

        strncpy(s->node_id, n->id, MAX_ID_LEN - 1);
        s->pkts_processed = atomic_load_explicit(&n->pkts_processed, memory_order_relaxed);
        s->pkts_dropped   = atomic_load_explicit(&n->pkts_dropped,   memory_order_relaxed);
        s->bytes_processed= atomic_load_explicit(&n->bytes_processed, memory_order_relaxed);
        s->core_id        = n->core_id;

        uint64_t delta = s->pkts_processed - s_prev_pkts[i];
        s->pps = (double)delta / dt;
        s_prev_pkts[i] = s->pkts_processed;

        /* Lcore utilization: delta busy / delta total over the last interval.
           Accumulate per-core so multiple nodes on the same core add together. */
        uint64_t busy = atomic_load_explicit(&n->busy_loops, memory_order_relaxed);
        uint64_t idle = atomic_load_explicit(&n->idle_loops, memory_order_relaxed);
        uint64_t d_busy = busy - s_prev_busy[i];
        uint64_t d_idle = idle - s_prev_idle[i];
        s_prev_busy[i] = busy;
        s_prev_idle[i] = idle;

        if (n->core_id < MAX_LCORES) {
            uint64_t d_total = d_busy + d_idle;
            if (d_total > 0) {
                /* Accumulate across nodes sharing the same core */
                snap->lcore_util[n->core_id] += (double)d_busy / (double)d_total;
            }
        }
    }

    /* Ring stats */
    for (int i = 0; i < pipeline->n_edges; i++) {
        ring_desc_t *e = &pipeline->edges[i];
        ring_stats_snap_t *s = &snap->rings[i];

        strncpy(s->ring_name, e->name, MAX_RING_NAME - 1);
        if (e->ring) {
            s->capacity = rte_ring_get_capacity(e->ring);
            s->used     = rte_ring_count(e->ring);
            s->fill_pct = s->capacity > 0 ? (double)s->used / s->capacity * 100.0 : 0.0;
        } else {
            s->capacity = 0; s->used = 0; s->fill_pct = 0;
        }
    }
}

static void *stats_thread_func(void *arg) {
    (void)arg;
    while (s_running && g_running) {
        if (s_pipeline) {
            stats_collect(s_pipeline, &g_latest_stats);
        }
        usleep(100000);  /* 100ms */
    }
    return NULL;
}

void stats_thread_start(pipeline_t *pipeline) {
    s_pipeline = pipeline;
    s_running  = 1;
    pthread_create(&s_thread, NULL, stats_thread_func, NULL);
}

void stats_thread_stop(void) {
    s_running = 0;
    pthread_join(s_thread, NULL);
}

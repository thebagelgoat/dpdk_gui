#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include <stdatomic.h>
#include "graph.h"

#define MAX_STAT_NODES  MAX_NODES
#define MAX_STAT_RINGS  MAX_EDGES
#define MAX_LCORES      4

typedef struct {
    char     node_id[MAX_ID_LEN];
    uint64_t pkts_processed;
    uint64_t pkts_dropped;
    uint64_t bytes_processed;
    uint32_t core_id;
    double   pps;      /* packets per second (rolling EWMA) */
    double   bps;      /* bytes per second (rolling EWMA) */
    uint64_t rule_hits[MAX_RULE_COUNTERS];
    int      n_rule_counters;
} node_stats_snap_t;

typedef struct {
    char     ring_name[MAX_RING_NAME];
    uint32_t capacity;
    uint32_t used;
    double   fill_pct;
    double   peak_fill_pct;   /* high-water mark since pipeline start */
} ring_stats_snap_t;

typedef struct {
    node_stats_snap_t nodes[MAX_STAT_NODES];
    ring_stats_snap_t rings[MAX_STAT_RINGS];
    int               n_nodes;
    int               n_rings;
    double            lcore_util[MAX_LCORES];  /* 0.0 – 1.0 */
    double            timestamp;               /* Unix time */
} stats_snapshot_t;

/* Initialize stats subsystem against a pipeline */
void stats_init(pipeline_t *pipeline);

/* Collect a fresh snapshot (called by stats thread every 100ms) */
void stats_collect(pipeline_t *pipeline, stats_snapshot_t *snap);

/* Start background stats collection thread (pthread) */
void stats_thread_start(pipeline_t *pipeline);
void stats_thread_stop(void);

/* Latest snapshot, updated every 100ms by the stats thread */
extern stats_snapshot_t g_latest_stats;

#endif /* STATS_H */

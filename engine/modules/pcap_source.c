#include "module_base.h"
#include "../node_out.h"
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_cycles.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define PCAP_MAGIC_USEC  0xa1b2c3d4u
#define PCAP_MAGIC_NSEC  0xa1b23c4du

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} pcap_global_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcap_rec_hdr_t;

typedef struct {
    uint64_t ts_us;
    uint32_t len;
    uint8_t *data;
} pcap_pkt_t;

typedef struct {
    char       file_path[512];
    double     speed_multiplier;  /* <=0 = full speed; >0 = timed replay */
    int        loop;

    pcap_pkt_t *pkts;
    uint32_t    n_pkts;
    uint32_t    pkt_idx;

    uint64_t    hz;
    uint64_t    start_cycles;
    uint64_t    first_ts_us;
    int         exhausted;
} pcap_source_cfg_t;

static void *pcap_source_parse(json_t *cfg) {
    pcap_source_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    const char *path = json_string_value(json_object_get(cfg, "file_path"));
    strncpy(c->file_path, path ? path : "", sizeof(c->file_path) - 1);
    json_t *spd = json_object_get(cfg, "speed_multiplier");
    c->speed_multiplier = spd ? json_number_value(spd) : 1.0;
    json_t *lp = json_object_get(cfg, "loop");
    c->loop = lp ? json_is_true(lp) : 0;
    return c;
}

static int pcap_source_init(node_desc_t *node) {
    pcap_source_cfg_t *c = node->module_cfg;

    if (c->file_path[0] == '\0') {
        fprintf(stderr, "pcap_source: no file_path configured\n");
        return -1;
    }

    FILE *fp = fopen(c->file_path, "rb");
    if (!fp) { perror("pcap_source: fopen"); return -1; }

    pcap_global_hdr_t gh;
    if (fread(&gh, sizeof(gh), 1, fp) != 1) {
        fprintf(stderr, "pcap_source: cannot read global header\n");
        fclose(fp); return -1;
    }
    int is_nsec = 0;
    if (gh.magic == PCAP_MAGIC_USEC)      is_nsec = 0;
    else if (gh.magic == PCAP_MAGIC_NSEC) is_nsec = 1;
    else {
        fprintf(stderr, "pcap_source: bad magic 0x%08x (not a pcap file)\n", gh.magic);
        fclose(fp); return -1;
    }

    /* First pass: count packets */
    uint32_t count = 0;
    pcap_rec_hdr_t rh;
    while (fread(&rh, sizeof(rh), 1, fp) == 1) {
        if (rh.incl_len > 65535) break;
        if (fseek(fp, rh.incl_len, SEEK_CUR) != 0) break;
        count++;
    }
    if (count == 0) {
        fprintf(stderr, "pcap_source: no packets in %s\n", c->file_path);
        fclose(fp); return -1;
    }

    c->pkts = calloc(count, sizeof(pcap_pkt_t));
    if (!c->pkts) {
        fprintf(stderr, "pcap_source: OOM\n");
        fclose(fp); return -1;
    }

    /* Second pass: load all packets */
    rewind(fp);
    fread(&gh, sizeof(gh), 1, fp);

    uint32_t loaded = 0;
    while (loaded < count && fread(&rh, sizeof(rh), 1, fp) == 1) {
        uint32_t len = rh.incl_len;
        if (len > 65535) break;
        uint8_t *buf = malloc(len);
        if (!buf || fread(buf, 1, len, fp) != len) { free(buf); break; }

        uint64_t ts_us = (uint64_t)rh.ts_sec * 1000000ULL;
        ts_us += is_nsec ? rh.ts_usec / 1000ULL : rh.ts_usec;

        c->pkts[loaded].ts_us = ts_us;
        c->pkts[loaded].len   = len;
        c->pkts[loaded].data  = buf;
        loaded++;
    }
    fclose(fp);

    c->n_pkts       = loaded;
    c->pkt_idx      = 0;
    c->exhausted    = 0;
    c->hz           = rte_get_timer_hz();
    c->start_cycles = rte_get_timer_cycles();
    c->first_ts_us  = c->pkts[0].ts_us;
    printf("pcap_source: loaded %u packets from %s\n", loaded, c->file_path);
    return 0;
}

static int pcap_source_process(node_desc_t *node) {
    pcap_source_cfg_t *c = node->module_cfg;
    if (c->exhausted || c->n_pkts == 0) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = 0;

    if (c->speed_multiplier <= 0.0) {
        /* Full-speed: emit up to BURST_SIZE per call */
        while (n < BURST_SIZE && c->pkt_idx < c->n_pkts) {
            pcap_pkt_t *p = &c->pkts[c->pkt_idx];
            struct rte_mbuf *m = rte_pktmbuf_alloc(g_pktmbuf_pool);
            if (!m) break;
            if (p->len > rte_pktmbuf_tailroom(m)) {
                rte_pktmbuf_free(m);
                c->pkt_idx++;
                atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
                continue;
            }
            char *dst = rte_pktmbuf_append(m, p->len);
            rte_memcpy(dst, p->data, p->len);
            m->pkt_len = m->data_len = p->len;
            pkts[n++] = m;
            c->pkt_idx++;
        }
    } else {
        /* Timed: emit packets whose pcap timestamp <= current deadline */
        uint64_t elapsed_cycles = rte_get_timer_cycles() - c->start_cycles;
        double elapsed_us = (double)elapsed_cycles / (double)c->hz * 1e6 * c->speed_multiplier;
        uint64_t deadline = c->first_ts_us + (uint64_t)elapsed_us;

        while (n < BURST_SIZE && c->pkt_idx < c->n_pkts) {
            pcap_pkt_t *p = &c->pkts[c->pkt_idx];
            if (p->ts_us > deadline) break;
            struct rte_mbuf *m = rte_pktmbuf_alloc(g_pktmbuf_pool);
            if (!m) break;
            if (p->len > rte_pktmbuf_tailroom(m)) {
                rte_pktmbuf_free(m);
                c->pkt_idx++;
                atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
                continue;
            }
            char *dst = rte_pktmbuf_append(m, p->len);
            rte_memcpy(dst, p->data, p->len);
            m->pkt_len = m->data_len = p->len;
            pkts[n++] = m;
            c->pkt_idx++;
        }
    }

    if (c->pkt_idx >= c->n_pkts) {
        if (c->loop) {
            c->pkt_idx      = 0;
            c->start_cycles = rte_get_timer_cycles();
        } else {
            c->exhausted = 1;
        }
    }

    if (n == 0) return 0;

    uint64_t bytes = 0;
    for (unsigned i = 0; i < n; i++) bytes += pkts[i]->pkt_len;
    unsigned sent = node_out(node, pkts, n);
    atomic_fetch_add_explicit(&node->pkts_processed,  sent,  memory_order_relaxed);
    atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
    return (int)n;
}

static void pcap_source_destroy(node_desc_t *node) {
    pcap_source_cfg_t *c = node->module_cfg;
    if (c->pkts) {
        for (uint32_t i = 0; i < c->n_pkts; i++) free(c->pkts[i].data);
        free(c->pkts);
        c->pkts = NULL;
    }
}

module_ops_t pcap_source_ops = {
    .init         = pcap_source_init,
    .process      = pcap_source_process,
    .destroy      = pcap_source_destroy,
    .parse_config = pcap_source_parse,
};

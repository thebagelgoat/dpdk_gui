#include "module_base.h"
#include <rte_mbuf.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <stdint.h>

/* Standard libpcap file format (no external dependency) */
#define PCAP_MAGIC        0xa1b2c3d4
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4
#define PCAP_LINKTYPE_EN10MB 1

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
    char     output_path[512];
    uint64_t max_file_size;   /* bytes */
    uint32_t snaplen;
    int      fd;
    uint64_t bytes_written;
    uint32_t pkt_count;
} pcap_cfg_t;

static void *pcap_parse(json_t *cfg) {
    pcap_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    const char *path = json_string_value(json_object_get(cfg, "output_path"));
    strncpy(c->output_path, path ? path : "/tmp/capture.pcap", sizeof(c->output_path) - 1);
    json_t *ms = json_object_get(cfg, "max_file_size_mb");
    c->max_file_size = ms ? (uint64_t)json_integer_value(ms) * 1024 * 1024 : 512ULL * 1024 * 1024;
    json_t *sl = json_object_get(cfg, "snaplen");
    c->snaplen = sl ? (uint32_t)json_integer_value(sl) : 65535;
    c->fd = -1;
    return c;
}

static int pcap_init(node_desc_t *node) {
    pcap_cfg_t *c = node->module_cfg;

    c->fd = open(c->output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (c->fd < 0) {
        perror("pcap_recorder: open");
        return -1;
    }

    pcap_global_hdr_t gh = {
        .magic         = PCAP_MAGIC,
        .version_major = PCAP_VERSION_MAJOR,
        .version_minor = PCAP_VERSION_MINOR,
        .thiszone      = 0,
        .sigfigs       = 0,
        .snaplen       = c->snaplen,
        .network       = PCAP_LINKTYPE_EN10MB,
    };
    if (write(c->fd, &gh, sizeof(gh)) != sizeof(gh)) {
        perror("pcap_recorder: write global header");
        close(c->fd); c->fd = -1;
        return -1;
    }
    c->bytes_written = sizeof(gh);
    c->pkt_count = 0;
    printf("PCAP recorder: writing to %s\n", c->output_path);
    return 0;
}

static int pcap_process(node_desc_t *node) {
    pcap_cfg_t *c = node->module_cfg;
    if (c->fd < 0) return 0;
    if (!node->input_rings[0] || !node->input_rings[0]->ring) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = rte_ring_dequeue_burst(node->input_rings[0]->ring,
                                         (void **)pkts, BURST_SIZE, NULL);
    if (n == 0) return 0;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t bytes = 0;
    unsigned processed = 0;

    for (unsigned i = 0; i < n; i++) {
        uint32_t pkt_len = pkts[i]->pkt_len;
        uint32_t cap_len = (pkt_len < c->snaplen) ? pkt_len : c->snaplen;

        /* Check file size limit */
        if (c->max_file_size > 0 &&
            c->bytes_written + sizeof(pcap_rec_hdr_t) + cap_len > c->max_file_size) {
            /* Silently stop writing but keep passing packets */
        } else {
            pcap_rec_hdr_t rh = {
                .ts_sec  = (uint32_t)ts.tv_sec,
                .ts_usec = (uint32_t)(ts.tv_nsec / 1000),
                .incl_len = cap_len,
                .orig_len = pkt_len,
            };
            write(c->fd, &rh, sizeof(rh));

            /* Write packet data (handle multi-segment mbufs) */
            struct rte_mbuf *seg = pkts[i];
            uint32_t left = cap_len;
            while (seg && left > 0) {
                uint32_t seg_len = seg->data_len < left ? seg->data_len : left;
                write(c->fd, rte_pktmbuf_mtod(seg, void *), seg_len);
                left -= seg_len;
                seg = seg->next;
            }
            c->bytes_written += sizeof(rh) + cap_len;
            c->pkt_count++;
        }

        /* Pass packet onwards if output is connected */
        bytes += pkt_len;
        if (node->output_rings[0] && node->output_rings[0]->ring) {
            if (rte_ring_enqueue(node->output_rings[0]->ring, pkts[i]) < 0) {
                rte_pktmbuf_free(pkts[i]);
                atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
                continue;
            }
        } else {
            rte_pktmbuf_free(pkts[i]);
        }
        processed++;
    }

    atomic_fetch_add_explicit(&node->pkts_processed,  processed, memory_order_relaxed);
    atomic_fetch_add_explicit(&node->bytes_processed, bytes,     memory_order_relaxed);
    return n;
}

static void pcap_destroy(node_desc_t *node) {
    pcap_cfg_t *c = node->module_cfg;
    if (c->fd >= 0) {
        fsync(c->fd);
        close(c->fd);
        c->fd = -1;
        printf("PCAP recorder: wrote %u packets (%llu bytes) to %s\n",
               c->pkt_count, (unsigned long long)c->bytes_written, c->output_path);
    }
}

module_ops_t pcap_recorder_ops = {
    .init         = pcap_init,
    .process      = pcap_process,
    .destroy      = pcap_destroy,
    .parse_config = pcap_parse,
};

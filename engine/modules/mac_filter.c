#include "module_base.h"
#include "../node_out.h"
#include <rte_ether.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_MAC_ENTRIES 64

typedef enum { ACTION_PASS, ACTION_DROP } filter_action_t;
typedef enum { MATCH_SRC, MATCH_DST, MATCH_EITHER } match_field_t;

typedef struct {
    struct rte_ether_addr addrs[MAX_MAC_ENTRIES];
    int             n_addrs;
    match_field_t   match_field;
    filter_action_t action;
    filter_action_t default_action;
} mac_filter_cfg_t;

/* Parse "aa:bb:cc:dd:ee:ff" into rte_ether_addr. Returns 0 on success. */
static int parse_mac(const char *s, struct rte_ether_addr *out) {
    unsigned v[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        fprintf(stderr, "mac_filter: invalid MAC address '%s'\n", s);
        return -1;
    }
    for (int i = 0; i < 6; i++) out->addr_bytes[i] = (uint8_t)v[i];
    return 0;
}

static void *mac_filter_parse(json_t *cfg) {
    mac_filter_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    const char *act = json_string_value(json_object_get(cfg, "action"));
    c->action = (act && !strcmp(act, "drop")) ? ACTION_DROP : ACTION_PASS;

    const char *def = json_string_value(json_object_get(cfg, "default_action"));
    c->default_action = (def && !strcmp(def, "drop")) ? ACTION_DROP : ACTION_PASS;

    const char *field = json_string_value(json_object_get(cfg, "match_field"));
    if (field && !strcmp(field, "dst"))        c->match_field = MATCH_DST;
    else if (field && !strcmp(field, "either")) c->match_field = MATCH_EITHER;
    else                                        c->match_field = MATCH_SRC;

    json_t *addrs = json_object_get(cfg, "addresses");
    if (addrs && json_is_array(addrs)) {
        size_t i; json_t *v;
        json_array_foreach(addrs, i, v) {
            if (c->n_addrs >= MAX_MAC_ENTRIES) break;
            const char *mac_s = json_string_value(v);
            if (!mac_s) continue;
            if (parse_mac(mac_s, &c->addrs[c->n_addrs]) == 0)
                c->n_addrs++;
        }
    }
    return c;
}

static inline int mac_in_list(mac_filter_cfg_t *c, const struct rte_ether_addr *addr) {
    for (int i = 0; i < c->n_addrs; i++) {
        if (rte_is_same_ether_addr(addr, &c->addrs[i])) return 1;
    }
    return 0;
}

static int mac_filter_process(node_desc_t *node) {
    mac_filter_cfg_t *c = node->module_cfg;
    if (!node->input_rings[0] || !node->input_rings[0]->ring) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = rte_ring_dequeue_burst(node->input_rings[0]->ring,
                                         (void **)pkts, BURST_SIZE, NULL);
    if (n == 0) return 0;

    struct rte_mbuf *pass[BURST_SIZE];
    unsigned n_pass = 0;
    uint64_t bytes  = 0;

    for (unsigned i = 0; i < n; i++) {
        struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkts[i], struct rte_ether_hdr *);

        int matched = 0;
        switch (c->match_field) {
        case MATCH_SRC:    matched = mac_in_list(c, &eth->src_addr);  break;
        case MATCH_DST:    matched = mac_in_list(c, &eth->dst_addr);  break;
        case MATCH_EITHER: matched = mac_in_list(c, &eth->src_addr) ||
                                     mac_in_list(c, &eth->dst_addr);  break;
        }

        filter_action_t verdict = matched ? c->action : c->default_action;
        if (verdict == ACTION_PASS) {
            bytes += pkts[i]->pkt_len;
            pass[n_pass++] = pkts[i];
        } else {
            rte_pktmbuf_free(pkts[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
        }
    }

    if (n_pass > 0) {
        unsigned enq = node_out(node, pass, n_pass);
        atomic_fetch_add_explicit(&node->pkts_processed,  enq,   memory_order_relaxed);
        atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
    }
    return n;
}

module_ops_t mac_filter_ops = {
    .init         = NULL,
    .process      = mac_filter_process,
    .destroy      = NULL,
    .parse_config = mac_filter_parse,
};

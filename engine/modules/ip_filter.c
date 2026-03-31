#include "module_base.h"
#include "../node_out.h"
#include <rte_ip.h>
#include <rte_ether.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

#define MAX_IP_RULES 32

typedef enum { ACTION_PASS, ACTION_DROP } filter_action_t;

typedef struct {
    uint32_t src_net;
    uint32_t src_mask;
    uint32_t dst_net;
    uint32_t dst_mask;
    filter_action_t action;
} ip_rule_t;

typedef struct {
    ip_rule_t       rules[MAX_IP_RULES];
    int             n_rules;
    filter_action_t default_action;
} ip_filter_cfg_t;

static int parse_cidr(const char *cidr, uint32_t *net, uint32_t *mask) {
    char buf[64];
    strncpy(buf, cidr, sizeof(buf) - 1);
    char *slash = strchr(buf, '/');
    int prefix = 32;
    if (slash) { *slash = '\0'; prefix = atoi(slash + 1); }
    struct in_addr addr;
    if (inet_pton(AF_INET, buf, &addr) != 1) return -1;
    *net  = ntohl(addr.s_addr);
    *mask = prefix == 0 ? 0 : (~0u << (32 - prefix));
    return 0;
}

static void *ip_filter_parse(json_t *cfg) {
    ip_filter_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    const char *def = json_string_value(json_object_get(cfg, "default_action"));
    c->default_action = (def && !strcmp(def, "drop")) ? ACTION_DROP : ACTION_PASS;

    json_t *rules = json_object_get(cfg, "rules");
    if (!rules || !json_is_array(rules)) return c;

    size_t i; json_t *rule;
    json_array_foreach(rules, i, rule) {
        if (c->n_rules >= MAX_IP_RULES) break;
        ip_rule_t *r = &c->rules[c->n_rules];
        const char *src = json_string_value(json_object_get(rule, "src_cidr"));
        const char *dst = json_string_value(json_object_get(rule, "dst_cidr"));
        const char *act = json_string_value(json_object_get(rule, "action"));
        if (!src || !dst) continue;
        if (parse_cidr(src, &r->src_net, &r->src_mask) < 0) continue;
        if (parse_cidr(dst, &r->dst_net, &r->dst_mask) < 0) continue;
        r->action = (act && !strcmp(act, "drop")) ? ACTION_DROP : ACTION_PASS;
        c->n_rules++;
    }
    return c;
}

static filter_action_t classify(ip_filter_cfg_t *c, uint32_t src_ip, uint32_t dst_ip,
                                _Atomic uint64_t *hits) {
    for (int i = 0; i < c->n_rules; i++) {
        ip_rule_t *r = &c->rules[i];
        if ((src_ip & r->src_mask) == r->src_net &&
            (dst_ip & r->dst_mask) == r->dst_net) {
            atomic_fetch_add_explicit(&hits[i], 1, memory_order_relaxed);
            return r->action;
        }
    }
    atomic_fetch_add_explicit(&hits[c->n_rules], 1, memory_order_relaxed); /* default slot */
    return c->default_action;
}

static int ip_filter_rule_count(void *cfg) {
    return ((ip_filter_cfg_t *)cfg)->n_rules + 1;  /* one per rule + one for default */
}

static int ip_filter_process(node_desc_t *node) {
    ip_filter_cfg_t *c = node->module_cfg;
    if (!node->input_rings[0] || !node->input_rings[0]->ring) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = rte_ring_dequeue_burst(node->input_rings[0]->ring,
                                         (void **)pkts, BURST_SIZE, NULL);
    if (n == 0) return 0;

    struct rte_mbuf *pass[BURST_SIZE];
    unsigned n_pass = 0;
    uint64_t bytes = 0;

    for (unsigned i = 0; i < n; i++) {
        struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkts[i], struct rte_ether_hdr *);
        uint16_t etype = rte_be_to_cpu_16(eth->ether_type);

        if (etype == RTE_ETHER_TYPE_IPV4) {
            struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
            uint32_t src = ntohl(ip->src_addr);
            uint32_t dst = ntohl(ip->dst_addr);
            if (classify(c, src, dst, node->rule_hits) == ACTION_PASS) {
                bytes += pkts[i]->pkt_len;
                pass[n_pass++] = pkts[i];
                continue;
            }
        } else if (c->default_action == ACTION_PASS) {
            /* Non-IPv4 follows default action */
            bytes += pkts[i]->pkt_len;
            pass[n_pass++] = pkts[i];
            continue;
        }
        rte_pktmbuf_free(pkts[i]);
        atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
    }

    if (n_pass > 0) {
        unsigned enq = node_out(node, pass, n_pass);
        atomic_fetch_add_explicit(&node->pkts_processed,  enq,   memory_order_relaxed);
        atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
    }

    return n;
}

module_ops_t ip_filter_ops = {
    .init         = NULL,
    .process      = ip_filter_process,
    .destroy      = NULL,
    .parse_config = ip_filter_parse,
    .rule_count   = ip_filter_rule_count,
};

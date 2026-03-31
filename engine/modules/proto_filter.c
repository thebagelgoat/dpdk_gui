#include "module_base.h"
#include "../node_out.h"
#include <rte_ether.h>
#include <rte_ip.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_PROTO_ENTRIES 32

typedef enum { ACTION_PASS, ACTION_DROP } filter_action_t;

/* A single entry is either an ethertype match or an IP-protocol match */
typedef enum { MATCH_ETHERTYPE, MATCH_IP_PROTO } match_kind_t;

typedef struct {
    match_kind_t kind;
    uint16_t     ethertype;   /* used when kind == MATCH_ETHERTYPE */
    uint8_t      ip_proto;    /* used when kind == MATCH_IP_PROTO  */
} proto_entry_t;

typedef struct {
    proto_entry_t   entries[MAX_PROTO_ENTRIES];
    int             n_entries;
    filter_action_t action;          /* action on MATCH */
    filter_action_t default_action;  /* action when nothing matches */
} proto_filter_cfg_t;

/* Map a protocol name string to a match entry. Returns 0 on success. */
static int name_to_entry(const char *name, proto_entry_t *e) {
    /* EtherType-based */
    if (!strcmp(name, "ipv4"))  { e->kind = MATCH_ETHERTYPE; e->ethertype = 0x0800; return 0; }
    if (!strcmp(name, "ipv6"))  { e->kind = MATCH_ETHERTYPE; e->ethertype = 0x86DD; return 0; }
    if (!strcmp(name, "arp"))   { e->kind = MATCH_ETHERTYPE; e->ethertype = 0x0806; return 0; }
    if (!strcmp(name, "mpls"))  { e->kind = MATCH_ETHERTYPE; e->ethertype = 0x8847; return 0; }
    if (!strcmp(name, "vlan"))  { e->kind = MATCH_ETHERTYPE; e->ethertype = 0x8100; return 0; }
    if (!strcmp(name, "pppoe")) { e->kind = MATCH_ETHERTYPE; e->ethertype = 0x8864; return 0; }
    /* IP-protocol-based (inside IPv4) */
    if (!strcmp(name, "icmp"))  { e->kind = MATCH_IP_PROTO; e->ip_proto =   1; return 0; }
    if (!strcmp(name, "igmp"))  { e->kind = MATCH_IP_PROTO; e->ip_proto =   2; return 0; }
    if (!strcmp(name, "tcp"))   { e->kind = MATCH_IP_PROTO; e->ip_proto =   6; return 0; }
    if (!strcmp(name, "udp"))   { e->kind = MATCH_IP_PROTO; e->ip_proto =  17; return 0; }
    if (!strcmp(name, "gre"))   { e->kind = MATCH_IP_PROTO; e->ip_proto =  47; return 0; }
    if (!strcmp(name, "esp"))   { e->kind = MATCH_IP_PROTO; e->ip_proto =  50; return 0; }
    if (!strcmp(name, "ah"))    { e->kind = MATCH_IP_PROTO; e->ip_proto =  51; return 0; }
    if (!strcmp(name, "icmp6")) { e->kind = MATCH_IP_PROTO; e->ip_proto =  58; return 0; }
    if (!strcmp(name, "ospf"))  { e->kind = MATCH_IP_PROTO; e->ip_proto =  89; return 0; }
    if (!strcmp(name, "sctp"))  { e->kind = MATCH_IP_PROTO; e->ip_proto = 132; return 0; }
    fprintf(stderr, "proto_filter: unknown protocol '%s'\n", name);
    return -1;
}

static void *proto_filter_parse(json_t *cfg) {
    proto_filter_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    const char *act = json_string_value(json_object_get(cfg, "action"));
    c->action = (act && !strcmp(act, "drop")) ? ACTION_DROP : ACTION_PASS;

    const char *def = json_string_value(json_object_get(cfg, "default_action"));
    c->default_action = (def && !strcmp(def, "drop")) ? ACTION_DROP : ACTION_PASS;

    json_t *protos = json_object_get(cfg, "protocols");
    if (protos && json_is_array(protos)) {
        size_t i; json_t *p;
        json_array_foreach(protos, i, p) {
            if (c->n_entries >= MAX_PROTO_ENTRIES) break;
            const char *name = json_string_value(p);
            if (!name) continue;
            proto_entry_t e = {0};
            if (name_to_entry(name, &e) == 0)
                c->entries[c->n_entries++] = e;
        }
    }
    return c;
}

static int proto_filter_process(node_desc_t *node) {
    proto_filter_cfg_t *c = node->module_cfg;
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
        uint16_t etype = rte_be_to_cpu_16(eth->ether_type);

        /* Skip VLAN tag to get the inner ethertype for IP-proto matching */
        uint8_t ip_proto = 0;
        int     has_ip_proto = 0;
        uint16_t inner_etype = etype;
        if (inner_etype == RTE_ETHER_TYPE_VLAN) {
            struct rte_vlan_hdr *vh = (struct rte_vlan_hdr *)(eth + 1);
            inner_etype = rte_be_to_cpu_16(vh->eth_proto);
        }
        if (inner_etype == RTE_ETHER_TYPE_IPV4) {
            struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)
                ((uint8_t *)eth + sizeof(*eth) +
                 (etype == RTE_ETHER_TYPE_VLAN ? sizeof(struct rte_vlan_hdr) : 0));
            ip_proto     = ip->next_proto_id;
            has_ip_proto = 1;
        }

        int matched = 0;
        for (int j = 0; j < c->n_entries; j++) {
            proto_entry_t *e = &c->entries[j];
            if (e->kind == MATCH_ETHERTYPE && e->ethertype == etype) {
                matched = 1; break;
            }
            if (e->kind == MATCH_IP_PROTO && has_ip_proto && e->ip_proto == ip_proto) {
                matched = 1; break;
            }
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

module_ops_t proto_filter_ops = {
    .init         = NULL,
    .process      = proto_filter_process,
    .destroy      = NULL,
    .parse_config = proto_filter_parse,
};

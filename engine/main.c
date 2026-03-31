#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_lcore.h>
#include <rte_launch.h>

#include "graph.h"
#include "worker.h"
#include "stats.h"
#include "ipc.h"

#define MBUF_POOL_NAME    "MBUF_POOL"
#define MBUF_POOL_SIZE    65535
#define MBUF_CACHE_SIZE   256
#define MBUF_PRIV_SIZE    0
#define RX_RING_SIZE      512
#define TX_RING_SIZE      512

static pipeline_t s_pipeline;
static lcore_ctx_t s_lcore_ctxs[MAX_NODES];
static int s_n_lcore_ctxs = 0;

static char s_graph_path[512] = "";
static char s_ipc_path[256]   = "/tmp/dpdk_engine.sock";

static void signal_handler(int sig) {
    (void)sig;
    printf("\nShutdown signal received.\n");
    g_running = 0;
}

static void configure_eth_ports(pipeline_t *pipeline) {
    bool port_used[RTE_MAX_ETHPORTS] = {false};

    for (int i = 0; i < pipeline->n_nodes; i++) {
        node_desc_t *n = &pipeline->nodes[i];
        if (n->type == MOD_NIC_RX || n->type == MOD_NIC_TX) {
            uint16_t pid = *((uint16_t *)n->module_cfg);  /* port_id is first field */
            if (pid < RTE_MAX_ETHPORTS) port_used[pid] = true;
        }
    }

    uint16_t nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0) {
        fprintf(stderr, "WARNING: No DPDK-enabled ports found. "
                "NICs may not be bound to a DPDK driver.\n");
        return;
    }

    struct rte_eth_conf port_conf = {
        .rxmode = {
            .mtu = RTE_ETHER_MAX_LEN,
        },
        .txmode = {
            .mq_mode = RTE_ETH_MQ_TX_NONE,
        },
    };

    for (uint16_t p = 0; p < RTE_MAX_ETHPORTS; p++) {
        if (!port_used[p]) continue;
        if (!rte_eth_dev_is_valid_port(p)) {
            fprintf(stderr, "WARNING: port %u referenced in graph but not available\n", p);
            continue;
        }

        struct rte_eth_dev_info dev_info;
        rte_eth_dev_info_get(p, &dev_info);

        int ret = rte_eth_dev_configure(p, 1, 1, &port_conf);
        if (ret < 0) { fprintf(stderr, "Cannot configure port %u: %d\n", p, ret); continue; }

        ret = rte_eth_rx_queue_setup(p, 0, RX_RING_SIZE,
                                      rte_eth_dev_socket_id(p), NULL, g_pktmbuf_pool);
        if (ret < 0) { fprintf(stderr, "Cannot setup RX queue for port %u: %d\n", p, ret); continue; }

        struct rte_eth_txconf txconf = dev_info.default_txconf;
        txconf.offloads = port_conf.txmode.offloads;
        ret = rte_eth_tx_queue_setup(p, 0, TX_RING_SIZE,
                                      rte_eth_dev_socket_id(p), &txconf);
        if (ret < 0) { fprintf(stderr, "Cannot setup TX queue for port %u: %d\n", p, ret); continue; }

        ret = rte_eth_dev_start(p);
        if (ret < 0) { fprintf(stderr, "Cannot start port %u: %d\n", p, ret); continue; }

        rte_eth_promiscuous_enable(p);

        struct rte_ether_addr addr;
        rte_eth_macaddr_get(p, &addr);
        printf("Port %u started: %02X:%02X:%02X:%02X:%02X:%02X\n", p,
               addr.addr_bytes[0], addr.addr_bytes[1], addr.addr_bytes[2],
               addr.addr_bytes[3], addr.addr_bytes[4], addr.addr_bytes[5]);
    }
}

static void stop_eth_ports(pipeline_t *pipeline) {
    bool port_used[RTE_MAX_ETHPORTS] = {false};
    for (int i = 0; i < pipeline->n_nodes; i++) {
        node_desc_t *n = &pipeline->nodes[i];
        if (n->type == MOD_NIC_RX || n->type == MOD_NIC_TX) {
            uint16_t pid = *((uint16_t *)n->module_cfg);
            if (pid < RTE_MAX_ETHPORTS) port_used[pid] = true;
        }
    }
    for (uint16_t p = 0; p < RTE_MAX_ETHPORTS; p++) {
        if (port_used[p] && rte_eth_dev_is_valid_port(p)) {
            rte_eth_dev_stop(p);
            rte_eth_dev_close(p);
        }
    }
}

/* Parse application-level args (after -- in EAL args) */
static void parse_app_args(int argc, char **argv) {
    static struct option long_opts[] = {
        { "graph", required_argument, NULL, 'g' },
        { "ipc",   required_argument, NULL, 'i' },
        { NULL, 0, NULL, 0 }
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "g:i:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'g': strncpy(s_graph_path, optarg, sizeof(s_graph_path) - 1); break;
            case 'i': strncpy(s_ipc_path,   optarg, sizeof(s_ipc_path) - 1);   break;
            default:  break;
        }
    }
}

int main(int argc, char **argv) {
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* EAL init consumes its args; remaining are app args */
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "EAL init failed: %d\n", ret);
        return 1;
    }
    argc -= ret;
    argv += ret;
    parse_app_args(argc, argv);

    if (s_graph_path[0] == '\0') {
        fprintf(stderr, "Usage: dpdk_engine [EAL args] -- --graph <graph.json> [--ipc <socket>]\n");
        rte_eal_cleanup();
        return 1;
    }

    printf("DPDK Packet Broker starting...\n");
    printf("  Graph: %s\n", s_graph_path);
    printf("  IPC:   %s\n", s_ipc_path);

    /* Create mbuf pool */
    g_pktmbuf_pool = rte_pktmbuf_pool_create(
        MBUF_POOL_NAME, MBUF_POOL_SIZE, MBUF_CACHE_SIZE,
        MBUF_PRIV_SIZE, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!g_pktmbuf_pool) {
        fprintf(stderr, "Cannot create mbuf pool. Do you have hugepages configured?\n");
        rte_eal_cleanup();
        return 1;
    }
    printf("Mbuf pool: %u buffers × %u bytes\n", MBUF_POOL_SIZE, RTE_MBUF_DEFAULT_BUF_SIZE);

    /* Parse graph JSON */
    if (parse_graph(s_graph_path, &s_pipeline) < 0) {
        fprintf(stderr, "Failed to parse graph: %s\n", s_graph_path);
        rte_eal_cleanup();
        return 1;
    }
    printf("Graph loaded: %d nodes, %d edges\n", s_pipeline.n_nodes, s_pipeline.n_edges);

    /* Configure Ethernet ports */
    configure_eth_ports(&s_pipeline);

    /* Build per-lcore worker contexts */
    if (worker_build_contexts(&s_pipeline, s_lcore_ctxs,
                               (int)(sizeof(s_lcore_ctxs) / sizeof(s_lcore_ctxs[0])),
                               &s_n_lcore_ctxs) < 0) {
        fprintf(stderr, "Failed to build worker contexts\n");
        rte_eal_cleanup();
        return 1;
    }

    /* Start stats thread */
    stats_init(&s_pipeline);
    stats_thread_start(&s_pipeline);

    /* Start IPC server */
    if (ipc_server_start(s_ipc_path, &s_pipeline) < 0) {
        fprintf(stderr, "Failed to start IPC server on %s\n", s_ipc_path);
        rte_eal_cleanup();
        return 1;
    }

    /* Launch workers on remote lcores.
       The main lcore context (if any) runs inline below.
       Other lcores get rte_eal_remote_launch. */
    lcore_ctx_t *main_ctx = NULL;
    unsigned main_lcore = rte_get_main_lcore();

    for (int c = 0; c < s_n_lcore_ctxs; c++) {
        lcore_ctx_t *ctx = &s_lcore_ctxs[c];
        if ((unsigned)ctx->core_id == main_lcore) {
            main_ctx = ctx;
        } else {
            if (!rte_lcore_is_enabled((unsigned)ctx->core_id)) {
                fprintf(stderr, "WARNING: core %d not enabled in EAL args. "
                        "Node(s) on this core will not run.\n", ctx->core_id);
                continue;
            }
            rte_eal_remote_launch(worker_lcore_func, ctx, (unsigned)ctx->core_id);
        }
    }

    /* Send "ready" to IPC client (blocks until client connects) */
    ipc_send_ready(s_pipeline.n_nodes, s_pipeline.n_edges);
    printf("Pipeline running. Waiting for shutdown...\n");

    /* Run main lcore worker (blocks until g_running == 0) */
    if (main_ctx) {
        worker_lcore_func(main_ctx);
    } else {
        /* No nodes on main lcore — just wait */
        while (g_running) usleep(100000);
    }

    printf("Shutdown: waiting for worker lcores...\n");
    rte_eal_mp_wait_lcore();

    /* Cleanup */
    stats_thread_stop();
    ipc_server_stop();
    stop_eth_ports(&s_pipeline);
    pipeline_free(&s_pipeline);
    rte_eal_cleanup();

    printf("DPDK Packet Broker stopped cleanly.\n");
    return 0;
}

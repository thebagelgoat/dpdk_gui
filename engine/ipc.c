#include "ipc.h"
#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <jansson.h>
#include <time.h>

static pthread_t    s_thread;
static volatile int s_running = 0;
static int          s_server_fd = -1;
static int          s_client_fd = -1;
static char         s_socket_path[256];
static pipeline_t  *s_pipeline = NULL;

/* Send a length-prefixed JSON message: [uint32_le len][json bytes] */
static int send_msg(int fd, json_t *obj) {
    char *buf = json_dumps(obj, JSON_COMPACT);
    if (!buf) return -1;
    uint32_t len = (uint32_t)strlen(buf);
    uint32_t len_le = htole32(len);

    if (write(fd, &len_le, 4) != 4 || (uint32_t)write(fd, buf, len) != len) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

/* Read a length-prefixed JSON message */
static json_t *recv_msg(int fd) {
    uint32_t len_le = 0;
    ssize_t n = read(fd, &len_le, 4);
    if (n <= 0) return NULL;
    uint32_t len = le32toh(len_le);
    if (len == 0 || len > 65536) return NULL;

    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    ssize_t got = 0;
    while (got < (ssize_t)len) {
        ssize_t r = read(fd, buf + got, len - got);
        if (r <= 0) { free(buf); return NULL; }
        got += r;
    }
    buf[len] = '\0';

    json_error_t err;
    json_t *obj = json_loads(buf, 0, &err);
    free(buf);
    return obj;
}

static json_t *build_stats_response(void) {
    stats_snapshot_t *snap = &g_latest_stats;

    json_t *root = json_object();
    json_object_set_new(root, "type", json_string("stats"));
    json_object_set_new(root, "timestamp", json_real(snap->timestamp));

    /* nodes */
    json_t *nodes_arr = json_array();
    for (int i = 0; i < snap->n_nodes; i++) {
        node_stats_snap_t *ns = &snap->nodes[i];
        json_t *obj = json_object();
        json_object_set_new(obj, "id",              json_string(ns->node_id));
        json_object_set_new(obj, "pkts_processed",  json_integer(ns->pkts_processed));
        json_object_set_new(obj, "pkts_dropped",    json_integer(ns->pkts_dropped));
        json_object_set_new(obj, "bytes_processed", json_integer(ns->bytes_processed));
        json_object_set_new(obj, "core_id",         json_integer(ns->core_id));
        json_object_set_new(obj, "pps",             json_real(ns->pps));
        json_array_append_new(nodes_arr, obj);
    }
    json_object_set_new(root, "nodes", nodes_arr);

    /* rings */
    json_t *rings_arr = json_array();
    for (int i = 0; i < snap->n_rings; i++) {
        ring_stats_snap_t *rs = &snap->rings[i];
        json_t *obj = json_object();
        json_object_set_new(obj, "name",     json_string(rs->ring_name));
        json_object_set_new(obj, "capacity", json_integer(rs->capacity));
        json_object_set_new(obj, "used",     json_integer(rs->used));
        json_object_set_new(obj, "fill_pct", json_real(rs->fill_pct));
        json_array_append_new(rings_arr, obj);
    }
    json_object_set_new(root, "rings", rings_arr);

    /* lcore utilization */
    json_t *util_arr = json_array();
    for (int i = 0; i < MAX_LCORES; i++) {
        json_array_append_new(util_arr, json_real(snap->lcore_util[i]));
    }
    json_object_set_new(root, "lcore_util", util_arr);

    return root;
}

static void *ipc_thread_func(void *arg) {
    (void)arg;

    while (s_running) {
        /* Wait for client connection */
        struct sockaddr_un caddr;
        socklen_t clen = sizeof(caddr);
        s_client_fd = accept(s_server_fd, (struct sockaddr *)&caddr, &clen);
        if (s_client_fd < 0) {
            if (s_running) perror("ipc accept");
            continue;
        }

        /* Serve this client */
        while (s_running) {
            json_t *cmd = recv_msg(s_client_fd);
            if (!cmd) break;

            const char *cmd_str = json_string_value(json_object_get(cmd, "cmd"));
            if (!cmd_str) { json_decref(cmd); break; }

            if (!strcmp(cmd_str, "get_stats")) {
                json_t *resp = build_stats_response();
                send_msg(s_client_fd, resp);
                json_decref(resp);
            } else if (!strcmp(cmd_str, "shutdown")) {
                g_running = 0;
                json_t *resp = json_object();
                json_object_set_new(resp, "type", json_string("ack"));
                json_object_set_new(resp, "msg", json_string("shutting down"));
                send_msg(s_client_fd, resp);
                json_decref(resp);
                json_decref(cmd);
                close(s_client_fd);
                s_client_fd = -1;
                goto done;
            }

            json_decref(cmd);
        }

        if (s_client_fd >= 0) {
            close(s_client_fd);
            s_client_fd = -1;
        }
    }

done:
    return NULL;
}

int ipc_server_start(const char *socket_path, pipeline_t *pipeline) {
    strncpy(s_socket_path, socket_path, sizeof(s_socket_path) - 1);
    s_pipeline = pipeline;

    unlink(socket_path);

    s_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s_server_fd < 0) { perror("ipc socket"); return -1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("ipc bind"); close(s_server_fd); return -1;
    }
    if (listen(s_server_fd, 1) < 0) {
        perror("ipc listen"); close(s_server_fd); return -1;
    }

    s_running = 1;
    pthread_create(&s_thread, NULL, ipc_thread_func, NULL);
    return 0;
}

void ipc_send_ready(int n_nodes, int n_rings) {
    /* Poll until client connects (up to 10s) */
    for (int i = 0; i < 100 && s_client_fd < 0; i++) {
        usleep(100000);
    }
    if (s_client_fd < 0) return;

    json_t *obj = json_object();
    json_object_set_new(obj, "type",    json_string("ready"));
    json_object_set_new(obj, "version", json_string("1.0"));
    json_object_set_new(obj, "n_nodes", json_integer(n_nodes));
    json_object_set_new(obj, "n_rings", json_integer(n_rings));
    send_msg(s_client_fd, obj);
    json_decref(obj);
}

void ipc_server_stop(void) {
    s_running = 0;
    if (s_server_fd >= 0) { close(s_server_fd); s_server_fd = -1; }
    if (s_client_fd >= 0) { close(s_client_fd); s_client_fd = -1; }
    pthread_join(s_thread, NULL);
    unlink(s_socket_path);
}

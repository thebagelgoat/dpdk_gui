#ifndef IPC_H
#define IPC_H

#include "graph.h"

/* Start Unix socket IPC server on given path.
   Accepts connections from the Python backend.
   Handles: get_stats, shutdown commands.
   Runs in a background pthread. */
int  ipc_server_start(const char *socket_path, pipeline_t *pipeline);
void ipc_server_stop(void);

/* Send the "ready" message to any connected client */
void ipc_send_ready(int n_nodes, int n_rings);

#endif /* IPC_H */

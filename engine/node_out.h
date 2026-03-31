#ifndef NODE_OUT_H
#define NODE_OUT_H

#include "graph.h"

/*
 * node_out — distribute a burst of processed packets to output rings.
 *
 * Behaviour is controlled by node->output_mode:
 *   OUT_FIRST        send all pkts to output_rings[0] (default)
 *   OUT_DUPLICATE    clone each packet and send to every connected output ring
 *   OUT_LOAD_BALANCE assign each packet to one output ring via RSS hash or
 *                    round-robin (node->lb_mode)
 *
 * Packets that cannot be enqueued (ring full, no ring connected, or clone
 * failure) are freed here and node->pkts_dropped is incremented.
 *
 * Returns the number of original packets successfully forwarded (for
 * OUT_DUPLICATE this is the number of originals that reached ≥1 output).
 */
unsigned node_out(node_desc_t *node, struct rte_mbuf **pkts, unsigned n);

#endif /* NODE_OUT_H */

from __future__ import annotations
from models.graph_schema import GraphSchema, GraphBody, HEAVY_TYPES


def validate_graph(graph: GraphSchema) -> list[str]:
    """Return list of validation error strings. Empty list = valid."""
    errors: list[str] = []
    g = graph.graph
    node_ids = {n.id for n in g.nodes}

    # Check edge references
    for edge in g.edges:
        if edge.source not in node_ids:
            errors.append(f"Edge '{edge.id}' references unknown source node '{edge.source}'")
        if edge.target not in node_ids:
            errors.append(f"Edge '{edge.id}' references unknown target node '{edge.target}'")

    # Check NIC port constraints
    for node in g.nodes:
        if node.type in ("nic_rx", "nic_tx"):
            pid = node.config.get("port_id", 0)
            if pid not in (0, 1):
                errors.append(f"Node '{node.id}': port_id must be 0 or 1, got {pid}")

    # Check nic_rx has no inputs, nic_tx has no outputs
    rx_ids = {n.id for n in g.nodes if n.type == "nic_rx"}
    tx_ids = {n.id for n in g.nodes if n.type == "nic_tx"}

    for edge in g.edges:
        if edge.target in rx_ids:
            errors.append(f"Node '{edge.target}' is a nic_rx and cannot have inputs")
        if edge.source in tx_ids:
            errors.append(f"Node '{edge.source}' is a nic_tx and cannot have outputs")

    # Check output_count matches actual edge count for duplicator/load_balancer
    for node in g.nodes:
        if node.type in ("duplicator", "load_balancer"):
            declared = node.config.get("output_count", 2)
            actual = sum(1 for e in g.edges if e.source == node.id)
            if actual != declared:
                errors.append(
                    f"Node '{node.id}' declares output_count={declared} "
                    f"but has {actual} output edge(s)"
                )

    # Check ring name uniqueness
    ring_names: dict[str, str] = {}
    for edge in g.edges:
        name = edge.ring.name or f"ring_{edge.source}_{edge.source_port}_{edge.target}_{edge.target_port}"
        if name in ring_names:
            errors.append(f"Duplicate ring name '{name}' on edges '{ring_names[name]}' and '{edge.id}'")
        else:
            ring_names[name] = edge.id

    # Check for cycles (DFS)
    if _has_cycle(g):
        errors.append("Graph contains a cycle which would cause deadlock")

    return errors


def _has_cycle(g: GraphBody) -> bool:
    """Simple DFS cycle detection."""
    adjacency: dict[str, list[str]] = {n.id: [] for n in g.nodes}
    for edge in g.edges:
        if edge.source in adjacency:
            adjacency[edge.source].append(edge.target)

    visited: set[str] = set()
    in_stack: set[str] = set()

    def dfs(node_id: str) -> bool:
        visited.add(node_id)
        in_stack.add(node_id)
        for neighbor in adjacency.get(node_id, []):
            if neighbor not in visited:
                if dfs(neighbor):
                    return True
            elif neighbor in in_stack:
                return True
        in_stack.discard(node_id)
        return False

    for node in g.nodes:
        if node.id not in visited:
            if dfs(node.id):
                return True
    return False

from __future__ import annotations
from models.graph_schema import GraphSchema, GraphBody


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

    # Source nodes (no inputs) and sink nodes (no outputs)
    SOURCE_TYPES = {"nic_rx", "pcap_source", "pkt_gen"}
    SINK_TYPES   = {"nic_tx"}

    source_ids = {n.id for n in g.nodes if n.type in SOURCE_TYPES}
    sink_ids   = {n.id for n in g.nodes if n.type in SINK_TYPES}

    for edge in g.edges:
        if edge.target in source_ids:
            node_type = next((n.type for n in g.nodes if n.id == edge.target), "?")
            errors.append(f"Node '{edge.target}' is a {node_type} (source) and cannot have inputs")
        if edge.source in sink_ids:
            errors.append(f"Node '{edge.source}' is a nic_tx and cannot have outputs")

    # pcap_source requires a file_path
    for node in g.nodes:
        if node.type == "pcap_source" and not node.config.get("file_path", ""):
            errors.append(f"Node '{node.id}' (pcap_source): file_path is required")

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

from __future__ import annotations
from models.graph_schema import GraphSchema, NodeSchema, HEAVY_TYPES, LIGHTWEIGHT_TYPES

AVAILABLE_CORES = [1, 2, 3]  # Core 0 reserved for OS + FastAPI


def allocate_cores(graph: GraphSchema) -> dict[str, int]:
    """
    Assign CPU cores to nodes.
    Returns {node_id: core_id}.
    Raises ValueError if too many heavy modules for available cores.
    """
    nodes = graph.graph.nodes
    heavy = [n for n in nodes if n.type in HEAVY_TYPES]
    light = [n for n in nodes if n.type in LIGHTWEIGHT_TYPES]

    if len(heavy) > len(AVAILABLE_CORES):
        raise ValueError(
            f"Graph has {len(heavy)} processing modules but only "
            f"{len(AVAILABLE_CORES)} data-plane cores available (cores 1-3). "
            f"Remove {len(heavy) - len(AVAILABLE_CORES)} heavy module(s) or "
            f"use counter/template modules which can share cores."
        )

    assignment: dict[str, int] = {}

    # Assign heavy modules to cores in order
    for i, node in enumerate(heavy):
        assignment[node.id] = AVAILABLE_CORES[i]

    # Pack lightweight modules onto the last used core
    if heavy:
        last_core = AVAILABLE_CORES[len(heavy) - 1]
    else:
        last_core = AVAILABLE_CORES[0]

    for node in light:
        assignment[node.id] = last_core

    return assignment


def apply_core_assignment(graph: GraphSchema, assignment: dict[str, int]) -> GraphSchema:
    """Return a new GraphSchema with core fields filled in."""
    new_nodes = []
    for node in graph.graph.nodes:
        data = node.model_dump()
        if node.id in assignment:
            data["core"] = assignment[node.id]
        # Auto-generate ring name if empty
        new_nodes.append(data)

    # Auto-generate ring names for edges that don't have one
    new_edges = []
    for edge in graph.graph.edges:
        data = edge.model_dump()
        if not data["ring"]["name"]:
            data["ring"]["name"] = (
                f"ring_{edge.source}_{edge.source_port}_{edge.target}_{edge.target_port}"
            )
        new_edges.append(data)

    return GraphSchema(
        version=graph.version,
        name=graph.name,
        graph={"nodes": new_nodes, "edges": new_edges},
    )

from __future__ import annotations
import json
import os
from pathlib import Path

from fastapi import APIRouter, HTTPException
from pydantic import ValidationError

from models.graph_schema import GraphSchema
from services.graph_validator import validate_graph

router = APIRouter()
GRAPHS_DIR = Path(__file__).parent.parent / "graphs"


def _graph_path(name: str) -> Path:
    # Sanitize: only allow alphanumeric, dash, underscore
    safe = "".join(c for c in name if c.isalnum() or c in "-_")
    if not safe:
        raise HTTPException(status_code=400, detail="Invalid graph name")
    return GRAPHS_DIR / f"{safe}.json"


@router.get("/")
async def list_graphs() -> list[str]:
    GRAPHS_DIR.mkdir(exist_ok=True)
    return [p.stem for p in GRAPHS_DIR.glob("*.json") if p.stem != "active"]


@router.get("/{name}")
async def get_graph(name: str) -> dict:
    path = _graph_path(name)
    if not path.exists():
        raise HTTPException(status_code=404, detail=f"Graph '{name}' not found")
    with open(path) as f:
        return json.load(f)


@router.post("/{name}")
async def save_graph(name: str, graph: GraphSchema) -> dict:
    errors = validate_graph(graph)
    if errors:
        raise HTTPException(status_code=422, detail={"validation_errors": errors})

    GRAPHS_DIR.mkdir(exist_ok=True)
    path = _graph_path(name)

    # Ensure ring names are auto-generated if empty
    for edge in graph.graph.edges:
        if not edge.ring.name:
            edge.ring.name = (
                f"ring_{edge.source}_{edge.source_port}_{edge.target}_{edge.target_port}"
            )

    graph.name = name
    with open(path, "w") as f:
        json.dump(graph.model_dump(), f, indent=2)

    return {"saved": name}


@router.delete("/{name}")
async def delete_graph(name: str) -> dict:
    path = _graph_path(name)
    if not path.exists():
        raise HTTPException(status_code=404, detail=f"Graph '{name}' not found")
    path.unlink()
    return {"deleted": name}

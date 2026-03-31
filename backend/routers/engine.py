from __future__ import annotations
import json
import os
from pathlib import Path

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from models.graph_schema import GraphSchema
from services.graph_validator import validate_graph
from services.core_allocator import allocate_cores, apply_core_assignment
from services.engine_manager import engine_manager
from services.stats_collector import start_stats_collector, stop_stats_collector
from websocket.manager import ws_manager

router = APIRouter()
GRAPHS_DIR = Path(__file__).parent.parent / "graphs"


class StartRequest(BaseModel):
    graph_name: str

class ReloadConfigRequest(BaseModel):
    node_id: str
    config: dict


@router.post("/start")
async def start_engine(req: StartRequest) -> dict:
    if engine_manager.is_running():
        raise HTTPException(status_code=409, detail="Engine already running")

    graph_path = GRAPHS_DIR / f"{req.graph_name}.json"
    if not graph_path.exists():
        raise HTTPException(status_code=404, detail=f"Graph '{req.graph_name}' not found")

    with open(graph_path) as f:
        raw = json.load(f)

    try:
        graph = GraphSchema.model_validate(raw)
    except Exception as e:
        raise HTTPException(status_code=422, detail=f"Graph parse error: {e}")

    errors = validate_graph(graph)
    if errors:
        raise HTTPException(status_code=422, detail={"validation_errors": errors})

    # Allocate cores
    try:
        assignment = allocate_cores(graph)
    except ValueError as e:
        raise HTTPException(status_code=422, detail=str(e))

    resolved = apply_core_assignment(graph, assignment)

    try:
        await engine_manager.start(resolved.model_dump(), req.graph_name)
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=str(e))

    # Begin stats streaming
    start_stats_collector(engine_manager, ws_manager)

    return {"status": "running", "graph_name": req.graph_name}


@router.post("/stop")
async def stop_engine() -> dict:
    stop_stats_collector()
    await engine_manager.stop()
    return {"status": "stopped"}


@router.post("/reload")
async def reload_node_config(req: ReloadConfigRequest) -> dict:
    if not engine_manager.is_running():
        raise HTTPException(status_code=409, detail="Engine not running")
    ok = await engine_manager.reload_node_config(req.node_id, req.config)
    if not ok:
        raise HTTPException(status_code=500, detail="Reload failed — node not found or config invalid")
    return {"status": "ok"}


@router.get("/status")
async def engine_status() -> dict:
    engine_manager.is_running()  # updates state if process has exited
    pid = engine_manager.proc.pid if engine_manager.proc else None
    return {
        "state": engine_manager.state,
        "graph_name": engine_manager.graph_name,
        "pid": pid,
    }

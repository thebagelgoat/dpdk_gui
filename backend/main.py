from __future__ import annotations
import logging
import os
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware

from routers import graphs, engine, system
from websocket.manager import ws_manager

logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

app = FastAPI(title="DPDK Packet Broker", version="1.0.0")

# Allow dev server (Vite on :5173) during development
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173", "http://127.0.0.1:5173"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(graphs.router, prefix="/api/graphs", tags=["graphs"])
app.include_router(engine.router, prefix="/api/engine", tags=["engine"])
app.include_router(system.router, prefix="/api/system", tags=["system"])


@app.websocket("/ws/stats")
async def stats_websocket(websocket: WebSocket) -> None:
    await ws_manager.connect(websocket)
    try:
        while True:
            # Keep-alive: wait for any client message (ping)
            await websocket.receive_text()
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)


# Serve built React app in production
FRONTEND_DIST = Path(__file__).parent.parent / "frontend" / "dist"
if FRONTEND_DIST.is_dir():
    app.mount("/", StaticFiles(directory=str(FRONTEND_DIST), html=True), name="static")


@app.on_event("shutdown")
async def shutdown_event() -> None:
    from services.engine_manager import engine_manager
    from services.stats_collector import stop_stats_collector
    stop_stats_collector()
    await engine_manager.stop()

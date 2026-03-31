from __future__ import annotations
import asyncio
import json
import logging
from fastapi import WebSocket

logger = logging.getLogger(__name__)


class WebSocketManager:
    def __init__(self) -> None:
        self._connections: list[WebSocket] = []

    async def connect(self, ws: WebSocket) -> None:
        await ws.accept()
        self._connections.append(ws)
        logger.info("WebSocket connected (%d total)", len(self._connections))

    def disconnect(self, ws: WebSocket) -> None:
        self._connections = [c for c in self._connections if c is not ws]
        logger.info("WebSocket disconnected (%d total)", len(self._connections))

    async def broadcast(self, data: dict) -> None:
        if not self._connections:
            return
        msg = json.dumps(data)
        dead: list[WebSocket] = []
        for ws in self._connections:
            try:
                await ws.send_text(msg)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.disconnect(ws)


# Global singleton
ws_manager = WebSocketManager()

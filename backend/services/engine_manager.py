from __future__ import annotations
import asyncio
import json
import os
import struct
import time
import logging
from pathlib import Path
from typing import Literal

logger = logging.getLogger(__name__)

ENGINE_BINARY = str(Path(__file__).parent.parent.parent / "engine" / "dpdk_engine")
ACTIVE_GRAPH_PATH = str(Path(__file__).parent.parent / "graphs" / "active.json")
IPC_SOCKET_PATH = "/tmp/dpdk_engine.sock"

NIC0 = "0000:00:10.0"
NIC1 = "0000:00:10.1"

EngineState = Literal["stopped", "starting", "running", "error"]


class EngineManager:
    def __init__(self) -> None:
        self.state: EngineState = "stopped"
        self.graph_name: str = ""
        self.proc: asyncio.subprocess.Process | None = None
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._ipc_lock = asyncio.Lock()

    async def start(self, graph_json: dict, graph_name: str) -> None:
        if self.state in ("starting", "running"):
            raise RuntimeError("Engine already running")

        self.state = "starting"
        self.graph_name = graph_name

        # Write active graph
        os.makedirs(os.path.dirname(ACTIVE_GRAPH_PATH), exist_ok=True)
        with open(ACTIVE_GRAPH_PATH, "w") as f:
            json.dump(graph_json, f, indent=2)

        # Clean up stale hugepage files from any previous crashed run
        import glob
        for f in glob.glob("/dev/hugepages/dpdk_broker*"):
            try:
                os.unlink(f)
            except OSError:
                pass

        # Build EAL arguments
        cmd = self._build_command()
        logger.info("Launching engine: %s", " ".join(cmd))

        try:
            self.proc = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,  # merge stderr into stdout
            )
        except FileNotFoundError:
            self.state = "error"
            raise RuntimeError(
                f"Engine binary not found at {ENGINE_BINARY}. Run ./build.sh first."
            )

        # Stream engine stdout to terminal in the background
        asyncio.create_task(self._log_engine_output())

        # Wait for IPC socket to appear (up to 15 seconds)
        for _ in range(150):
            if os.path.exists(IPC_SOCKET_PATH):
                break
            if self.proc.returncode is not None:
                stdout, stderr = await self.proc.communicate()
                self.state = "error"
                raise RuntimeError(
                    f"Engine exited during startup (rc={self.proc.returncode}):\n"
                    f"{stderr.decode()}"
                )
            await asyncio.sleep(0.1)
        else:
            await self._kill()
            self.state = "error"
            raise RuntimeError("Timeout waiting for engine IPC socket")

        # Connect to IPC socket
        try:
            self._reader, self._writer = await asyncio.open_unix_connection(IPC_SOCKET_PATH)
        except Exception as e:
            await self._kill()
            self.state = "error"
            raise RuntimeError(f"Cannot connect to engine IPC: {e}")

        # Read "ready" message
        try:
            msg = await asyncio.wait_for(self._recv_msg(), timeout=10.0)
            if not msg or msg.get("type") != "ready":
                raise RuntimeError(f"Unexpected ready message: {msg}")
        except asyncio.TimeoutError:
            await self._kill()
            self.state = "error"
            raise RuntimeError("Timeout waiting for engine ready message")

        self.state = "running"
        logger.info("Engine running: %d nodes, %d rings",
                    msg.get("n_nodes", 0), msg.get("n_rings", 0))

    async def stop(self) -> None:
        if self.state not in ("running", "starting"):
            return
        try:
            if self._writer and not self._writer.is_closing():
                await self._send_msg({"cmd": "shutdown"})
                await asyncio.wait_for(self.proc.wait(), timeout=5.0)
        except Exception:
            pass
        await self._kill()
        self.state = "stopped"

    async def get_stats(self) -> dict | None:
        if self.state != "running" or not self._writer:
            return None
        try:
            async with self._ipc_lock:
                await self._send_msg({"cmd": "get_stats"})
                return await asyncio.wait_for(self._recv_msg(), timeout=1.0)
        except Exception as e:
            logger.warning("Stats fetch failed: %s", e)
            return None

    async def reload_node_config(self, node_id: str, config: dict) -> bool:
        if self.state != "running" or not self._writer:
            return False
        try:
            async with self._ipc_lock:
                await self._send_msg({"cmd": "reload_config", "node_id": node_id, "config": config})
                resp = await asyncio.wait_for(self._recv_msg(), timeout=2.0)
                return resp is not None and resp.get("type") == "ack"
        except Exception as e:
            logger.warning("Reload config failed: %s", e)
            return False

    async def ping(self) -> bool:
        if self.state != "running" or not self._writer:
            return False
        try:
            async with self._ipc_lock:
                await self._send_msg({"cmd": "ping"})
                resp = await asyncio.wait_for(self._recv_msg(), timeout=2.0)
                return resp is not None and resp.get("type") == "pong"
        except Exception:
            return False

    def mark_crashed(self) -> None:
        self.state = "error"

    def is_running(self) -> bool:
        if self.proc and self.proc.returncode is not None:
            self.state = "stopped"
        return self.state == "running"

    def _build_command(self) -> list[str]:
        import psutil
        # All cores except 0 (reserved for OS + FastAPI)
        n = psutil.cpu_count(logical=True)
        lcores = ",".join(str(c) for c in range(1, n))

        cmd = [
            ENGINE_BINARY,
            "-l", lcores,
            "--main-lcore", "1",
            "--proc-type", "primary",
            "--file-prefix", "dpdk_broker",
        ]

        # Add NIC devices if they are bound to a DPDK driver
        for nic in (NIC0, NIC1):
            driver_path = f"/sys/bus/pci/devices/{nic}/driver"
            if os.path.islink(driver_path):
                driver = os.path.basename(os.readlink(driver_path))
                if driver in ("vfio-pci", "uio_pci_generic", "igb_uio"):
                    cmd += ["-a", nic]

        cmd += [
            "--",
            "--graph", ACTIVE_GRAPH_PATH,
            "--ipc", IPC_SOCKET_PATH,
        ]
        return cmd

    async def _log_engine_output(self) -> None:
        """Stream engine stdout/stderr to the terminal prefixed with [ENGINE]."""
        if not self.proc or not self.proc.stdout:
            return
        try:
            async for line in self.proc.stdout:
                print(f"[ENGINE] {line.decode(errors='replace').rstrip()}", flush=True)
        except Exception:
            pass

    async def _send_msg(self, obj: dict) -> None:
        data = json.dumps(obj).encode()
        length = struct.pack("<I", len(data))
        self._writer.write(length + data)
        await self._writer.drain()

    async def _recv_msg(self) -> dict | None:
        hdr = await self._reader.readexactly(4)
        length = struct.unpack("<I", hdr)[0]
        if length == 0 or length > 65536:
            return None
        data = await self._reader.readexactly(length)
        return json.loads(data)

    async def _kill(self) -> None:
        if self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:
                pass
            self._reader = None
            self._writer = None
        if self.proc and self.proc.returncode is None:
            try:
                self.proc.terminate()
                await asyncio.wait_for(self.proc.wait(), timeout=3.0)
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass
        self.proc = None
        if os.path.exists(IPC_SOCKET_PATH):
            os.unlink(IPC_SOCKET_PATH)


# Global singleton
engine_manager = EngineManager()

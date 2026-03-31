from __future__ import annotations
import asyncio
import logging
import psutil

logger = logging.getLogger(__name__)

_task: asyncio.Task | None = None

# Prime psutil so the first real call returns a valid delta
psutil.cpu_percent(percpu=True)


async def _stats_loop(engine_mgr, ws_manager) -> None:
    while engine_mgr.is_running():
        stats = await engine_mgr.get_stats()
        if stats:
            # Replace the C-side lcore_util with real per-core OS percentages.
            # This works regardless of whether NICs are in DPDK or kernel mode.
            cpu_pcts = psutil.cpu_percent(percpu=True)
            stats["lcore_util"] = [p / 100.0 for p in cpu_pcts]
            stats["cpu_count"] = len(cpu_pcts)
            await ws_manager.broadcast(stats)
        await asyncio.sleep(0.2)  # 200ms


def start_stats_collector(engine_mgr, ws_manager) -> None:
    global _task
    if _task and not _task.done():
        return
    _task = asyncio.create_task(_stats_loop(engine_mgr, ws_manager))
    logger.info("Stats collector started")


def stop_stats_collector() -> None:
    global _task
    if _task and not _task.done():
        _task.cancel()
        _task = None

from __future__ import annotations
import asyncio
import logging
import psutil

logger = logging.getLogger(__name__)

_task: asyncio.Task | None = None

# Prime psutil so the first real call returns a valid delta
psutil.cpu_percent(percpu=True)

_FAILURE_THRESHOLD = 10  # consecutive 200ms ticks = 2s before declaring crashed


async def _stats_loop(engine_mgr, ws_manager) -> None:
    consecutive_failures = 0

    while engine_mgr.is_running():
        stats = await engine_mgr.get_stats()
        if stats:
            consecutive_failures = 0
            # Replace the C-side lcore_util with real per-core OS percentages.
            cpu_pcts = psutil.cpu_percent(percpu=True)
            stats["lcore_util"] = [p / 100.0 for p in cpu_pcts]
            stats["cpu_count"] = len(cpu_pcts)
            await ws_manager.broadcast(stats)
        else:
            consecutive_failures += 1
            if consecutive_failures >= _FAILURE_THRESHOLD:
                logger.warning("Engine IPC unresponsive — marking as crashed")
                engine_mgr.mark_crashed()
                await ws_manager.broadcast({"type": "engine_status", "status": "error",
                                            "msg": "Engine stopped responding"})
                return

        await asyncio.sleep(0.2)  # 200ms

    # Engine process exited normally or was stopped — notify frontend if it was unexpected
    if engine_mgr.state not in ("stopped",):
        await ws_manager.broadcast({"type": "engine_status", "status": engine_mgr.state,
                                    "msg": "Engine process exited"})


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

from pydantic import BaseModel


class NodeStats(BaseModel):
    id: str
    pkts_processed: int = 0
    pkts_dropped: int = 0
    bytes_processed: int = 0
    core_id: int = 0
    pps: float = 0.0


class RingStats(BaseModel):
    name: str
    capacity: int = 0
    used: int = 0
    fill_pct: float = 0.0


class StatsMessage(BaseModel):
    type: str = "stats"
    timestamp: float = 0.0
    nodes: list[NodeStats] = []
    rings: list[RingStats] = []
    lcore_util: list[float] = []

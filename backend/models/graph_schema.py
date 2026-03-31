from __future__ import annotations
from typing import Literal, Optional, Any
from pydantic import BaseModel, Field, field_validator


class RingConfig(BaseModel):
    name: str = ""
    size: int = Field(default=1024, ge=256, le=8192)

    @field_validator("size")
    @classmethod
    def must_be_power_of_two(cls, v: int) -> int:
        if v & (v - 1) != 0:
            raise ValueError(f"Ring size {v} must be a power of 2")
        return v


class Position(BaseModel):
    x: float = 0.0
    y: float = 0.0


ModuleType = Literal[
    "nic_rx", "nic_tx", "ip_filter", "vlan_filter", "port_filter",
    "pcap_recorder", "counter", "template",
]

HEAVY_TYPES = {
    "nic_rx", "nic_tx", "ip_filter", "vlan_filter", "port_filter",
    "pcap_recorder",
}
LIGHTWEIGHT_TYPES = {"counter", "template"}


class NodeSchema(BaseModel):
    id: str
    type: ModuleType
    label: str = ""
    core: int = Field(default=1, ge=1, le=3)
    config: dict[str, Any] = Field(default_factory=dict)
    position: Position = Field(default_factory=Position)


class EdgeSchema(BaseModel):
    id: str
    source: str
    source_port: int = Field(default=0, ge=0, lt=4)
    target: str
    target_port: int = Field(default=0, ge=0, lt=4)
    ring: RingConfig = Field(default_factory=RingConfig)


class GraphBody(BaseModel):
    nodes: list[NodeSchema] = Field(default_factory=list)
    edges: list[EdgeSchema] = Field(default_factory=list)


class GraphSchema(BaseModel):
    version: str = "1"
    name: str
    graph: GraphBody = Field(default_factory=GraphBody)

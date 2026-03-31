import React from "react";
import { Handle, Position, NodeProps } from "reactflow";
import { useEngineStore } from "../../../store/engineStore";
import { useUIStore } from "../../../store/uiStore";

interface ModuleNodeData {
  label: string;
  moduleType: string;
  config: Record<string, unknown>;
  color: string;
  maxInputs: number;
  maxOutputs: number;
  stats?: { pps: number; pkts_dropped: number };
}

const formatPps = (pps: number) => {
  if (pps >= 1e6) return `${(pps / 1e6).toFixed(1)}M pps`;
  if (pps >= 1e3) return `${(pps / 1e3).toFixed(1)}K pps`;
  return `${pps.toFixed(0)} pps`;
};

export default function ModuleNode({ id, data, selected }: NodeProps<ModuleNodeData>) {
  const stats = useEngineStore((s) => s.stats);
  const setSelectedNode = useUIStore((s) => s.setSelectedNode);

  const nodeStats = stats?.nodes.find((n) => n.id === id);
  const isRunning = useEngineStore((s) => s.status === "running");

  const inputHandles = data.maxInputs > 0
    ? Array.from({ length: data.maxInputs }, (_, i) => i)
    : [];
  const outputHandles = data.maxOutputs > 0
    ? Array.from({ length: data.maxOutputs }, (_, i) => i)
    : [];

  const inputSpacing = data.maxInputs > 1 ? 100 / (data.maxInputs + 1) : 50;
  const outputSpacing = data.maxOutputs > 1 ? 100 / (data.maxOutputs + 1) : 50;

  return (
    <div
      onClick={() => setSelectedNode(id)}
      style={{
        background: "#1e2035",
        border: `2px solid ${selected ? "#60a5fa" : data.color}`,
        borderRadius: 8,
        minWidth: 140,
        cursor: "pointer",
        boxShadow: selected ? `0 0 12px ${data.color}60` : "0 2px 8px #00000060",
        userSelect: "none",
      }}
    >
      {/* Header */}
      <div
        style={{
          background: data.color,
          borderRadius: "6px 6px 0 0",
          padding: "6px 10px",
          fontSize: 11,
          fontWeight: 700,
          textTransform: "uppercase",
          letterSpacing: 1,
          color: "#fff",
        }}
      >
        {data.moduleType.replace("_", " ")}
      </div>

      {/* Body */}
      <div style={{ padding: "8px 10px" }}>
        <div style={{ fontSize: 13, fontWeight: 600, color: "#e2e8f0", marginBottom: 4 }}>
          {data.label}
        </div>
        {isRunning && nodeStats && (
          <div style={{ fontSize: 11, color: "#94a3b8" }}>
            <span style={{ color: "#34d399" }}>{formatPps(nodeStats.pps)}</span>
            {nodeStats.pkts_dropped > 0 && (
              <span style={{ color: "#f87171", marginLeft: 6 }}>
                ▼{nodeStats.pkts_dropped.toLocaleString()} drop
              </span>
            )}
          </div>
        )}
      </div>

      {/* Input handles (left side) */}
      {inputHandles.map((i) => (
        <Handle
          key={`in-${i}`}
          id={`in-${i}`}
          type="target"
          position={Position.Left}
          style={{
            top: `${inputSpacing * (i + 1)}%`,
            background: "#475569",
            border: "2px solid #94a3b8",
            width: 10,
            height: 10,
          }}
        />
      ))}

      {/* Output handles (right side) */}
      {outputHandles.map((i) => (
        <Handle
          key={`out-${i}`}
          id={`out-${i}`}
          type="source"
          position={Position.Right}
          style={{
            top: `${outputSpacing * (i + 1)}%`,
            background: data.color,
            border: "2px solid #e2e8f0",
            width: 10,
            height: 10,
          }}
        />
      ))}
    </div>
  );
}

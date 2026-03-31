import React, { useState } from "react";
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
  const [collapsed, setCollapsed] = useState(false);

  const nodeStats = stats?.nodes.find((n) => n.id === id);
  const isRunning = useEngineStore((s) => s.status === "running");

  // Single input handle, single output handle — multiple edges connect to the same handle
  const inputHandles  = data.maxInputs  > 0 ? [0] : [];
  const outputHandles = data.maxOutputs > 0 ? [0] : [];

  const inputSpacing  = 50;
  const outputSpacing = 50;

  return (
    <div
      onClick={() => setSelectedNode(id)}
      style={{
        background: "#1e2035",
        border: `2px solid ${selected ? data.color : data.color + "99"}`,
        borderRadius: 10,
        minWidth: 150,
        cursor: "pointer",
        boxShadow: selected
          ? `0 0 0 2px ${data.color}80, 0 8px 32px ${data.color}40`
          : "0 4px 20px rgba(0,0,0,0.55)",
        transition: "border-color 0.15s ease, box-shadow 0.15s ease",
        userSelect: "none",
      }}
    >
      {/* Header with gradient */}
      <div
        style={{
          background: `linear-gradient(135deg, ${data.color}55 0%, #1e2035 100%)`,
          borderBottom: collapsed ? "none" : `1px solid ${data.color}44`,
          borderRadius: collapsed ? "8px 8px 8px 8px" : "8px 8px 0 0",
          padding: "7px 10px",
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          gap: 6,
        }}
      >
        {/* Colored accent dot */}
        <div style={{
          width: 8, height: 8, borderRadius: "50%",
          background: data.color, flexShrink: 0,
        }} />
        <div style={{
          fontSize: 11, fontWeight: 700,
          textTransform: "uppercase", letterSpacing: 1,
          color: "#e2e8f0", flex: 1,
        }}>
          {data.moduleType.replace(/_/g, " ")}
        </div>
        {/* Collapse toggle */}
        <button
          className="nodrag"
          onClick={(e) => { e.stopPropagation(); setCollapsed((c) => !c); }}
          style={{
            background: "none", border: "none", color: "#64748b",
            cursor: "pointer", padding: "0 2px", fontSize: 12, lineHeight: 1,
          }}
          title={collapsed ? "Expand" : "Collapse"}
        >
          {collapsed ? "▸" : "▾"}
        </button>
      </div>

      {/* Body */}
      {!collapsed && (
        <div style={{ padding: "8px 12px 10px" }}>
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
          {!isRunning && (
            <div style={{ fontSize: 10, color: "#475569" }}>
              {data.moduleType === "nic_rx" || data.moduleType === "nic_tx"
                ? `Port ${(data.config as any).port_id ?? 0}`
                : "\u00a0"}
            </div>
          )}
        </div>
      )}

      {/* Input handles (left side) */}
      {inputHandles.map((i) => (
        <Handle
          key={`in-${i}`}
          id={`in-${i}`}
          type="target"
          position={Position.Left}
          style={{
            top: collapsed ? "50%" : `${inputSpacing * (i + 1)}%`,
            background: "#334155",
            border: `2px solid ${data.color}88`,
            width: 14,
            height: 14,
            borderRadius: "50%",
            transition: "width 0.15s ease, height 0.15s ease",
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
            top: collapsed ? "50%" : `${outputSpacing * (i + 1)}%`,
            background: data.color,
            border: "2px solid #e2e8f080",
            width: 14,
            height: 14,
            borderRadius: "50%",
            transition: "width 0.15s ease, height 0.15s ease",
          }}
        />
      ))}
    </div>
  );
}

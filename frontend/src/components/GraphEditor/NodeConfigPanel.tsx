import React, { useState, useEffect } from "react";
import { useGraphStore } from "../../store/graphStore";
import { useUIStore } from "../../store/uiStore";

export default function NodeConfigPanel() {
  const { selectedNodeId, setSelectedNode } = useUIStore();
  const { nodes, updateNodeConfig, updateNodeLabel } = useGraphStore();

  const node = nodes.find((n) => n.id === selectedNodeId);
  const [configText, setConfigText] = useState("");
  const [label, setLabel] = useState("");
  const [error, setError] = useState("");

  useEffect(() => {
    if (node) {
      setConfigText(JSON.stringify(node.data.config, null, 2));
      setLabel(node.data.label as string);
      setError("");
    }
  }, [selectedNodeId, node]);

  if (!node) return null;

  const handleApply = () => {
    try {
      const parsed = JSON.parse(configText);
      updateNodeConfig(node.id, parsed);
      updateNodeLabel(node.id, label);
      setError("");
    } catch (e) {
      setError("Invalid JSON: " + (e as Error).message);
    }
  };

  return (
    <div
      style={{
        width: 280,
        background: "#0f1117",
        borderLeft: "1px solid #2d3748",
        display: "flex",
        flexDirection: "column",
        flexShrink: 0,
        overflow: "hidden",
      }}
    >
      {/* Header */}
      <div
        style={{
          padding: "10px 12px",
          borderBottom: "1px solid #2d3748",
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
        }}
      >
        <div style={{ fontSize: 11, fontWeight: 700, textTransform: "uppercase", color: "#64748b", letterSpacing: 1 }}>
          Configure Node
        </div>
        <button
          onClick={() => setSelectedNode(null)}
          style={{ background: "none", border: "none", color: "#64748b", cursor: "pointer", fontSize: 16 }}
        >
          ×
        </button>
      </div>

      <div style={{ padding: 12, overflowY: "auto", flex: 1 }}>
        {/* Node type badge */}
        <div style={{ marginBottom: 12 }}>
          <div
            style={{
              display: "inline-block",
              background: node.data.color as string,
              color: "#fff",
              borderRadius: 4,
              padding: "2px 8px",
              fontSize: 11,
              fontWeight: 700,
              textTransform: "uppercase",
            }}
          >
            {node.data.moduleType as string}
          </div>
          <div style={{ fontSize: 10, color: "#475569", marginTop: 4 }}>ID: {node.id}</div>
        </div>

        {/* Label */}
        <div style={{ marginBottom: 12 }}>
          <label style={{ fontSize: 11, color: "#94a3b8", display: "block", marginBottom: 4 }}>Label</label>
          <input
            value={label}
            onChange={(e) => setLabel(e.target.value)}
            style={inputStyle}
          />
        </div>

        {/* Config JSON editor */}
        <div style={{ marginBottom: 12 }}>
          <label style={{ fontSize: 11, color: "#94a3b8", display: "block", marginBottom: 4 }}>
            Config (JSON)
          </label>
          <textarea
            value={configText}
            onChange={(e) => setConfigText(e.target.value)}
            rows={12}
            style={{ ...inputStyle, resize: "vertical", fontFamily: "monospace", fontSize: 12 }}
          />
          {error && (
            <div style={{ color: "#f87171", fontSize: 11, marginTop: 4 }}>{error}</div>
          )}
        </div>

        <button onClick={handleApply} style={btnStyle}>
          Apply
        </button>
      </div>
    </div>
  );
}

const inputStyle: React.CSSProperties = {
  width: "100%",
  background: "#1e2435",
  border: "1px solid #374151",
  borderRadius: 4,
  color: "#e2e8f0",
  padding: "6px 8px",
  fontSize: 12,
  outline: "none",
};

const btnStyle: React.CSSProperties = {
  width: "100%",
  background: "#2563eb",
  border: "none",
  borderRadius: 4,
  color: "#fff",
  padding: "8px",
  fontSize: 13,
  fontWeight: 600,
  cursor: "pointer",
};

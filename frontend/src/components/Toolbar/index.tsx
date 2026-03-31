import React, { useState, useEffect } from "react";
import { useGraphStore } from "../../store/graphStore";
import { useEngineStore } from "../../store/engineStore";
import { useUIStore } from "../../store/uiStore";
import { saveGraph, listGraphs, getGraph } from "../../api/graphs";
import StatusBadge from "./StatusBadge";

export default function Toolbar() {
  const { graphName, setGraphName, toGraphSchema, loadGraph, isDirty, markSaved } = useGraphStore();
  const { status, play, stop, errorMsg } = useEngineStore();
  const { setGraphListOpen, graphListOpen } = useUIStore();
  const [savedGraphs, setSavedGraphs] = useState<string[]>([]);
  const [msg, setMsg] = useState("");

  const isRunning = status === "running" || status === "starting";

  const refreshList = async () => {
    try {
      const list = await listGraphs();
      setSavedGraphs(list);
    } catch {
      /* ignore */
    }
  };

  useEffect(() => {
    refreshList();
  }, []);

  const handleSave = async () => {
    try {
      const schema = toGraphSchema();
      await saveGraph(graphName, schema);
      markSaved();
      setMsg("Saved!");
      setTimeout(() => setMsg(""), 2000);
      refreshList();
    } catch (e: unknown) {
      const err = e as { response?: { data?: { detail?: unknown } } };
      setMsg("Save failed: " + JSON.stringify(err?.response?.data?.detail ?? e));
    }
  };

  const handleLoad = async (name: string) => {
    try {
      const schema = await getGraph(name);
      loadGraph(schema);
      setGraphListOpen(false);
    } catch (e) {
      setMsg("Load failed");
    }
  };

  const handlePlay = async () => {
    await handleSave();
    await play(graphName);
  };

  return (
    <div
      style={{
        height: 52,
        background: "#0a0c14",
        borderBottom: "1px solid #2d3748",
        display: "flex",
        alignItems: "center",
        padding: "0 16px",
        gap: 10,
        flexShrink: 0,
        zIndex: 10,
      }}
    >
      {/* Logo */}
      <div style={{ fontSize: 14, fontWeight: 700, color: "#60a5fa", marginRight: 8, whiteSpace: "nowrap" }}>
        DPDK Packet Broker
      </div>

      {/* Graph name */}
      <input
        value={graphName}
        onChange={(e) => setGraphName(e.target.value)}
        placeholder="graph name"
        style={{
          background: "#1e2435",
          border: "1px solid #374151",
          borderRadius: 4,
          color: "#e2e8f0",
          padding: "4px 8px",
          fontSize: 13,
          width: 150,
          outline: "none",
        }}
      />
      {isDirty && <span style={{ color: "#f59e0b", fontSize: 11 }}>●</span>}

      {/* Save */}
      <button onClick={handleSave} disabled={isRunning} style={btnStyle("#1e2435")}>
        Save
      </button>

      {/* Load dropdown */}
      <div style={{ position: "relative" }}>
        <button
          onClick={() => { refreshList(); setGraphListOpen(!graphListOpen); }}
          style={btnStyle("#1e2435")}
        >
          Load ▾
        </button>
        {graphListOpen && (
          <div
            style={{
              position: "absolute",
              top: "100%",
              left: 0,
              background: "#1e2435",
              border: "1px solid #374151",
              borderRadius: 4,
              zIndex: 100,
              minWidth: 160,
              marginTop: 2,
            }}
          >
            {savedGraphs.length === 0 && (
              <div style={{ padding: "8px 12px", color: "#64748b", fontSize: 12 }}>No saved graphs</div>
            )}
            {savedGraphs.map((name) => (
              <div
                key={name}
                onClick={() => handleLoad(name)}
                style={{
                  padding: "8px 12px",
                  cursor: "pointer",
                  fontSize: 13,
                  color: "#e2e8f0",
                  borderBottom: "1px solid #2d3748",
                }}
                onMouseEnter={(e) => ((e.currentTarget as HTMLDivElement).style.background = "#2d3748")}
                onMouseLeave={(e) => ((e.currentTarget as HTMLDivElement).style.background = "transparent")}
              >
                {name}
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Spacer */}
      <div style={{ flex: 1 }} />

      {/* Inline error from engine */}
      {status === "error" && errorMsg && (
        <span style={{ fontSize: 11, color: "#f87171", maxWidth: 300, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}
              title={errorMsg}>
          Error: {errorMsg}
        </span>
      )}

      {/* Save/load status message */}
      {msg && <span style={{ fontSize: 11, color: "#94a3b8" }}>{msg}</span>}

      {/* Status badge */}
      <StatusBadge />

      {/* Play / Stop */}
      {!isRunning ? (
        <button
          onClick={handlePlay}
          style={btnStyle("#16a34a", true)}
        >
          ▶ Play
        </button>
      ) : (
        <button
          onClick={stop}
          style={btnStyle("#dc2626", true)}
        >
          ■ Stop
        </button>
      )}
    </div>
  );
}

function btnStyle(bg: string, bold = false): React.CSSProperties {
  return {
    background: bg,
    border: "1px solid #374151",
    borderRadius: 4,
    color: "#e2e8f0",
    padding: "5px 12px",
    fontSize: 12,
    fontWeight: bold ? 700 : 400,
    cursor: "pointer",
    whiteSpace: "nowrap",
  };
}

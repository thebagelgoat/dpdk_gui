import React, { useState, useCallback, useRef } from "react";
import { useEngineStore } from "../../store/engineStore";
import CoreUsageBar from "./CoreUsageBar";
import PacketCounter from "./PacketCounter";

export default function StatsPanel() {
  const { status, stats } = useEngineStore();
  const [height, setHeight] = useState(100);
  const dragStart = useRef<{ y: number; h: number } | null>(null);

  const onMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    dragStart.current = { y: e.clientY, h: height };

    const onMove = (ev: MouseEvent) => {
      if (!dragStart.current) return;
      const delta = dragStart.current.y - ev.clientY;
      setHeight(Math.max(60, Math.min(400, dragStart.current.h + delta)));
    };
    const onUp = () => {
      dragStart.current = null;
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [height]);

  if (status !== "running") return null;

  const totalPps = stats ? stats.nodes.reduce((s, n) => s + n.pps, 0) : 0;
  const totalDrops = stats ? stats.nodes.reduce((s, n) => s + n.pkts_dropped, 0) : 0;

  return (
    <div style={{ flexShrink: 0 }}>
      {/* Drag handle */}
      <div
        onMouseDown={onMouseDown}
        style={{
          height: 5,
          background: "#2d3748",
          cursor: "ns-resize",
          borderTop: "1px solid #374151",
        }}
        title="Drag to resize"
      />
    <div
      style={{
        height,
        background: "#0a0c14",
        padding: "8px 16px",
        display: "flex",
        gap: 24,
        alignItems: "center",
        overflowX: "auto",
        overflowY: "auto",
      }}
    >
      {/* Total pkt/s */}
      <PacketCounter pps={totalPps} drops={totalDrops} />

      {/* Core bars — one per physical core, count driven by what the backend reports */}
      <div style={{ display: "flex", gap: 4, alignItems: "flex-end", flexWrap: "wrap", maxWidth: 300 }}>
        {(stats?.lcore_util ?? []).map((util, i) => (
          <CoreUsageBar key={i} coreId={i} utilization={util} />
        ))}
      </div>

      {!stats && (
        <div style={{ color: "#64748b", fontSize: 12 }}>Waiting for stats...</div>
      )}

      {/* Per-node stats */}
      <div style={{ display: "flex", gap: 12, alignItems: "center", overflowX: "auto" }}>
        {(stats?.nodes ?? []).map((n) => (
          <div
            key={n.id}
            style={{
              background: "#1e2435",
              border: "1px solid #2d3748",
              borderRadius: 4,
              padding: "4px 10px",
              fontSize: 11,
              minWidth: 100,
              textAlign: "center",
            }}
          >
            <div style={{ color: "#94a3b8", marginBottom: 2, fontSize: 10 }}>{n.id}</div>
            <div style={{ color: "#34d399", fontWeight: 700 }}>
              {n.pps >= 1e6 ? `${(n.pps / 1e6).toFixed(1)}M` :
               n.pps >= 1e3 ? `${(n.pps / 1e3).toFixed(1)}K` :
               n.pps.toFixed(0)} pps
            </div>
            {n.pkts_dropped > 0 && (
              <div style={{ color: "#f87171", fontSize: 10 }}>
                ▼ {n.pkts_dropped.toLocaleString()} drop
              </div>
            )}
          </div>
        ))}
      </div>
    </div>
    </div>
  );
}

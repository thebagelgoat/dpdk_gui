import React from "react";

interface Props {
  coreId: number;
  utilization: number;  // 0.0 to 1.0
}

export default function CoreUsageBar({ coreId, utilization }: Props) {
  const pct = Math.round(utilization * 100);
  const color = pct > 90 ? "#ef4444" : pct > 60 ? "#f59e0b" : "#22c55e";

  return (
    <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 4 }}>
      <div
        style={{
          width: 32,
          height: 60,
          background: "#1e2435",
          border: "1px solid #374151",
          borderRadius: 4,
          position: "relative",
          overflow: "hidden",
        }}
      >
        <div
          style={{
            position: "absolute",
            bottom: 0,
            left: 0,
            right: 0,
            height: `${pct}%`,
            background: color,
            transition: "height 0.3s ease",
          }}
        />
      </div>
      <div style={{ fontSize: 9, color: "#64748b", textAlign: "center" }}>
        <div>C{coreId}</div>
        <div style={{ color }}>{pct}%</div>
      </div>
    </div>
  );
}

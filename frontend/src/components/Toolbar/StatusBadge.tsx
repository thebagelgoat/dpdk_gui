import React from "react";
import { useEngineStore } from "../../store/engineStore";

const STATUS_COLORS: Record<string, string> = {
  stopped: "#475569",
  starting: "#f59e0b",
  running: "#22c55e",
  error: "#ef4444",
};

export default function StatusBadge() {
  const { status, errorMsg } = useEngineStore();
  const color = STATUS_COLORS[status] ?? "#475569";

  return (
    <div
      title={errorMsg || status}
      style={{
        display: "flex",
        alignItems: "center",
        gap: 6,
        background: "#1e2435",
        border: `1px solid ${color}`,
        borderRadius: 4,
        padding: "4px 10px",
        fontSize: 11,
        fontWeight: 700,
        color,
        textTransform: "uppercase",
        letterSpacing: 1,
      }}
    >
      <div
        style={{
          width: 6,
          height: 6,
          borderRadius: "50%",
          background: color,
          animation: status === "running" ? "pulse 2s infinite" : undefined,
        }}
      />
      {status}
    </div>
  );
}

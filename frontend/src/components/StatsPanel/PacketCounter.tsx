import React from "react";

interface Props {
  pps: number;
  drops: number;
}

const fmt = (n: number) => {
  if (n >= 1e9) return `${(n / 1e9).toFixed(2)}G`;
  if (n >= 1e6) return `${(n / 1e6).toFixed(2)}M`;
  if (n >= 1e3) return `${(n / 1e3).toFixed(1)}K`;
  return n.toFixed(0);
};

export default function PacketCounter({ pps, drops }: Props) {
  return (
    <div
      style={{
        background: "#1e2435",
        border: "1px solid #2d3748",
        borderRadius: 6,
        padding: "8px 14px",
        minWidth: 110,
        textAlign: "center",
      }}
    >
      <div style={{ fontSize: 10, color: "#64748b", marginBottom: 4, letterSpacing: 1 }}>
        THROUGHPUT
      </div>
      <div style={{ fontSize: 20, fontWeight: 700, color: "#34d399" }}>
        {fmt(pps)}
      </div>
      <div style={{ fontSize: 10, color: "#64748b" }}>pps</div>
      {drops > 0 && (
        <div style={{ fontSize: 10, color: "#f87171", marginTop: 4 }}>
          ▼ {fmt(drops)} drop
        </div>
      )}
    </div>
  );
}

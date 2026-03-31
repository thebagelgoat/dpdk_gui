import React from "react";
import { MODULE_DEFS, ModuleType } from "../../types/graph";

interface Props {
  onDragStart: (type: string) => void;
}

const CATEGORIES: { label: string; types: ModuleType[] }[] = [
  { label: "I / O",     types: ["nic_rx", "nic_tx"] },
  { label: "Filters",   types: ["ip_filter", "vlan_filter", "port_filter", "proto_filter"] },
  { label: "Recording", types: ["pcap_recorder"] },
  { label: "Utility",   types: ["counter", "template"] },
];

export default function NodePalette({ onDragStart }: Props) {
  return (
    <div
      style={{
        width: 190,
        background: "#0b0d18",
        borderRight: "1px solid #2d3748",
        overflowY: "auto",
        flexShrink: 0,
        display: "flex",
        flexDirection: "column",
      }}
    >
      <div
        style={{
          padding: "10px 12px 8px",
          fontSize: 10,
          fontWeight: 700,
          textTransform: "uppercase",
          letterSpacing: 1.5,
          color: "#475569",
          borderBottom: "1px solid #1e2435",
        }}
      >
        Modules
      </div>

      {CATEGORIES.map((cat) => {
        const defs = cat.types.map((t) => MODULE_DEFS.find((d) => d.type === t)!).filter(Boolean);
        return (
          <div key={cat.label}>
            {/* Category header */}
            <div
              style={{
                padding: "8px 12px 4px",
                fontSize: 9,
                fontWeight: 700,
                textTransform: "uppercase",
                letterSpacing: 1.5,
                color: "#64748b",
                background: "#0a0c14",
                borderBottom: "1px solid #1e2435",
              }}
            >
              {cat.label}
            </div>

            {defs.map((def) => (
              <div
                key={def.type}
                draggable
                onDragStart={(e) => {
                  e.dataTransfer.setData("moduleType", def.type);
                  onDragStart(def.type);
                }}
                title={def.description}
                style={{
                  padding: "7px 12px",
                  cursor: "grab",
                  borderBottom: "1px solid #13151f",
                  display: "flex",
                  alignItems: "center",
                  gap: 9,
                  transition: "background 0.1s ease",
                }}
                onMouseEnter={(e) => {
                  (e.currentTarget as HTMLDivElement).style.background = "#1a1d2e";
                }}
                onMouseLeave={(e) => {
                  (e.currentTarget as HTMLDivElement).style.background = "transparent";
                }}
              >
                {/* Circle dot */}
                <div
                  style={{
                    width: 10,
                    height: 10,
                    borderRadius: "50%",
                    background: def.color,
                    flexShrink: 0,
                    boxShadow: `0 0 4px ${def.color}80`,
                  }}
                />
                <div>
                  <div style={{ fontSize: 12, fontWeight: 600, color: "#cbd5e0" }}>{def.label}</div>
                  <div style={{ fontSize: 9.5, color: "#475569", marginTop: 1, lineHeight: 1.3 }}>
                    {def.description}
                  </div>
                </div>
              </div>
            ))}
          </div>
        );
      })}
    </div>
  );
}

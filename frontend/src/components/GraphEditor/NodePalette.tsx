import React from "react";
import { MODULE_DEFS } from "../../types/graph";

interface Props {
  onDragStart: (type: string) => void;
}

export default function NodePalette({ onDragStart }: Props) {
  return (
    <div
      style={{
        width: 180,
        background: "#0f1117",
        borderRight: "1px solid #2d3748",
        overflowY: "auto",
        flexShrink: 0,
      }}
    >
      <div
        style={{
          padding: "10px 12px",
          fontSize: 10,
          fontWeight: 700,
          textTransform: "uppercase",
          letterSpacing: 1,
          color: "#64748b",
          borderBottom: "1px solid #2d3748",
        }}
      >
        Modules
      </div>
      {MODULE_DEFS.map((def) => (
        <div
          key={def.type}
          draggable
          onDragStart={(e) => {
            e.dataTransfer.setData("moduleType", def.type);
            onDragStart(def.type);
          }}
          title={def.description}
          style={{
            padding: "8px 12px",
            cursor: "grab",
            borderBottom: "1px solid #1e2435",
            display: "flex",
            alignItems: "center",
            gap: 8,
          }}
          onMouseEnter={(e) => {
            (e.currentTarget as HTMLDivElement).style.background = "#1e2435";
          }}
          onMouseLeave={(e) => {
            (e.currentTarget as HTMLDivElement).style.background = "transparent";
          }}
        >
          <div
            style={{
              width: 10,
              height: 10,
              borderRadius: 2,
              background: def.color,
              flexShrink: 0,
            }}
          />
          <div>
            <div style={{ fontSize: 12, fontWeight: 600, color: "#e2e8f0" }}>{def.label}</div>
            <div style={{ fontSize: 10, color: "#64748b", marginTop: 1 }}>{def.description}</div>
          </div>
        </div>
      ))}
    </div>
  );
}

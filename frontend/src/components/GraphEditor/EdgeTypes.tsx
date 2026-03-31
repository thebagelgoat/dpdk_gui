import React, { useState } from "react";
import {
  BaseEdge,
  EdgeLabelRenderer,
  getBezierPath,
  EdgeProps,
} from "reactflow";
import { useEngineStore } from "../../store/engineStore";

interface AnimatedEdgeData {
  ring?: { name: string; size: number };
}

export default function AnimatedEdge({
  id,
  sourceX,
  sourceY,
  targetX,
  targetY,
  sourcePosition,
  targetPosition,
  data,
}: EdgeProps<AnimatedEdgeData>) {
  const stats = useEngineStore((s) => s.stats);
  const isRunning = useEngineStore((s) => s.status === "running");
  const [hovered, setHovered] = useState(false);

  const ringName = data?.ring?.name ?? "";
  const ringStats = isRunning ? stats?.rings.find((r) => r.name === ringName) : undefined;
  const fillPct = ringStats?.fill_pct ?? 0;

  const edgeColor =
    fillPct > 80 ? "#ef4444" : fillPct > 50 ? "#f59e0b" : "#22c55e";
  const baseColor = isRunning ? edgeColor : "#475569";
  const strokeWidth = hovered ? 3.5 : 2;

  const [edgePath, labelX, labelY] = getBezierPath({
    sourceX, sourceY, sourcePosition,
    targetX, targetY, targetPosition,
  });

  return (
    <>
      {/* Invisible wide hit area for easier hovering */}
      <path
        d={edgePath}
        fill="none"
        stroke="transparent"
        strokeWidth={18}
        onMouseEnter={() => setHovered(true)}
        onMouseLeave={() => setHovered(false)}
        style={{ cursor: "pointer" }}
      />
      <BaseEdge
        id={id}
        path={edgePath}
        style={{
          stroke: baseColor,
          strokeWidth,
          strokeDasharray: isRunning ? "8 4" : undefined,
          animation: isRunning ? "dashdraw 0.5s linear infinite" : undefined,
          transition: "stroke 0.3s ease, stroke-width 0.15s ease",
          pointerEvents: "none",
        }}
        markerEnd="url(#arrowhead)"
      />
      {isRunning && ringStats && (
        <EdgeLabelRenderer>
          <div
            style={{
              position: "absolute",
              transform: `translate(-50%, -50%) translate(${labelX}px,${labelY}px)`,
              background: "#1e2035",
              border: `1px solid ${edgeColor}`,
              borderRadius: 4,
              padding: "2px 6px",
              fontSize: 10,
              color: edgeColor,
              fontWeight: 700,
              pointerEvents: "none",
              display: "flex",
              flexDirection: "column",
              alignItems: "center",
              gap: 1,
            }}
          >
            <span>{fillPct.toFixed(0)}%</span>
            {ringStats.peak_fill_pct > fillPct + 5 && (
              <span style={{ color: "#94a3b8", fontSize: 9, fontWeight: 400 }}>
                ▲{ringStats.peak_fill_pct.toFixed(0)}%
              </span>
            )}
          </div>
        </EdgeLabelRenderer>
      )}
    </>
  );
}

import React from "react";
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

  const ringName = data?.ring?.name ?? "";
  const ringStats = isRunning ? stats?.rings.find((r) => r.name === ringName) : undefined;
  const fillPct = ringStats?.fill_pct ?? 0;

  // Color: green → yellow → red based on fill %
  const edgeColor =
    fillPct > 80 ? "#ef4444" : fillPct > 50 ? "#f59e0b" : "#22c55e";
  const baseColor = isRunning ? edgeColor : "#475569";

  const [edgePath, labelX, labelY] = getBezierPath({
    sourceX, sourceY, sourcePosition,
    targetX, targetY, targetPosition,
  });

  return (
    <>
      <BaseEdge
        id={id}
        path={edgePath}
        style={{ stroke: baseColor, strokeWidth: 2 }}
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
            }}
          >
            {fillPct.toFixed(0)}%
          </div>
        </EdgeLabelRenderer>
      )}
    </>
  );
}

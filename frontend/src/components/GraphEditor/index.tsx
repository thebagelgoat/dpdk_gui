import React, { useCallback, useRef } from "react";
import ReactFlow, {
  Background,
  Controls,
  MiniMap,
  BackgroundVariant,
} from "reactflow";
import "reactflow/dist/style.css";

import { useGraphStore } from "../../store/graphStore";
import { useUIStore } from "../../store/uiStore";
import ModuleNode from "./nodes/ModuleNode";
import AnimatedEdge from "./EdgeTypes";
import NodePalette from "./NodePalette";
import NodeConfigPanel from "./NodeConfigPanel";
import type { ModuleType } from "../../types/graph";

const nodeTypes = { module: ModuleNode };
const edgeTypes = { animated: AnimatedEdge };

export default function GraphEditor() {
  const { nodes, edges, onNodesChange, onEdgesChange, onConnect, addNode } = useGraphStore();
  const { selectedNodeId, configPanelOpen } = useUIStore();
  const reactFlowWrapper = useRef<HTMLDivElement>(null);
  const dragTypeRef = useRef<string>("");

  const onDragOver = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = "move";
  }, []);

  const onDrop = useCallback(
    (e: React.DragEvent) => {
      e.preventDefault();
      const type = e.dataTransfer.getData("moduleType") || dragTypeRef.current;
      if (!type || !reactFlowWrapper.current) return;

      const bounds = reactFlowWrapper.current.getBoundingClientRect();
      const position = {
        x: e.clientX - bounds.left - 70,
        y: e.clientY - bounds.top - 30,
      };
      addNode(type as ModuleType, position);
    },
    [addNode]
  );

  return (
    <div style={{ flex: 1, display: "flex", overflow: "hidden" }}>
      <NodePalette onDragStart={(t) => { dragTypeRef.current = t; }} />

      <div ref={reactFlowWrapper} style={{ flex: 1, position: "relative" }}>
        <ReactFlow
          nodes={nodes}
          edges={edges}
          nodeTypes={nodeTypes}
          edgeTypes={edgeTypes}
          onNodesChange={onNodesChange}
          onEdgesChange={onEdgesChange}
          onConnect={onConnect}
          onDragOver={onDragOver}
          onDrop={onDrop}
          fitView
          deleteKeyCode="Delete"
          style={{ background: "#0d0f1a" }}
        >
          <Background color="#1e2435" variant={BackgroundVariant.Dots} gap={20} />
          <Controls style={{ background: "#1e2435", border: "1px solid #374151" }} />
          <MiniMap
            style={{ background: "#0f1117", border: "1px solid #374151" }}
            nodeColor={(n) => (n.data?.color as string) ?? "#374151"}
          />
        </ReactFlow>
      </div>

      {configPanelOpen && selectedNodeId && <NodeConfigPanel />}
    </div>
  );
}

import React from "react";
import Toolbar from "./components/Toolbar";
import GraphEditor from "./components/GraphEditor";
import StatsPanel from "./components/StatsPanel";
import { useStatsWebSocket } from "./api/websocket";

export default function App() {
  useStatsWebSocket();

  return (
    <>
      <style>{`
        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.4; }
        }
        .react-flow__controls button {
          background: #1e2435 !important;
          border-color: #374151 !important;
          color: #94a3b8 !important;
          fill: #94a3b8 !important;
        }
        .react-flow__controls button:hover {
          background: #2d3748 !important;
        }
        ::-webkit-scrollbar { width: 6px; height: 6px; }
        ::-webkit-scrollbar-track { background: #0f1117; }
        ::-webkit-scrollbar-thumb { background: #374151; border-radius: 3px; }
      `}</style>

      <Toolbar />
      <div style={{ flex: 1, display: "flex", overflow: "hidden" }}>
        <GraphEditor />
      </div>
      <StatsPanel />
    </>
  );
}

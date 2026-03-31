import { useEffect, useRef } from "react";
import { useEngineStore } from "../store/engineStore";
import { getEngineStatus } from "./engine";
import type { StatsMessage } from "../types/stats";

export function useStatsWebSocket() {
  const wsRef = useRef<WebSocket | null>(null);
  const { setStats, status, setStatus } = useEngineStore();

  // WebSocket for live stats
  useEffect(() => {
    if (status !== "running") {
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
      return;
    }

    const wsUrl = `ws://${window.location.host}/ws/stats`;
    const ws = new WebSocket(wsUrl);
    wsRef.current = ws;

    ws.onmessage = (evt) => {
      try {
        const data: StatsMessage = JSON.parse(evt.data);
        if (data.type === "stats") {
          setStats(data);
        }
      } catch {
        /* ignore parse errors */
      }
    };

    // Send ping every 10s to keep connection alive
    const ping = setInterval(() => {
      if (ws.readyState === WebSocket.OPEN) ws.send("ping");
    }, 10000);

    return () => {
      clearInterval(ping);
      ws.close();
      wsRef.current = null;
    };
  }, [status, setStats]);

  // Poll engine status every 2s to detect crashes or unexpected stops
  useEffect(() => {
    if (status !== "running") return;

    const poll = setInterval(async () => {
      try {
        const s = await getEngineStatus();
        if (s.state !== "running") {
          setStatus(s.state as "stopped" | "error", "Engine stopped unexpectedly");
        }
      } catch {
        /* ignore transient errors */
      }
    }, 2000);

    return () => clearInterval(poll);
  }, [status, setStatus]);
}

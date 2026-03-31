import { create } from "zustand";
import type { StatsMessage } from "../types/stats";
import { startEngine, stopEngine } from "../api/engine";
import { useGraphStore } from "./graphStore";

type EngineStatus = "stopped" | "starting" | "running" | "error";

interface EngineStore {
  status: EngineStatus;
  graphName: string;
  stats: StatsMessage | null;
  errorMsg: string;
  setStats: (stats: StatsMessage) => void;
  play: (graphName: string) => Promise<void>;
  stop: () => Promise<void>;
  setStatus: (status: EngineStatus, msg?: string) => void;
}

export const useEngineStore = create<EngineStore>((set) => ({
  status: "stopped",
  graphName: "",
  stats: null,
  errorMsg: "",

  setStats: (stats) => set({ stats }),

  setStatus: (status, msg = "") => set({ status, errorMsg: msg }),

  play: async (graphName) => {
    useGraphStore.getState().clearNodeErrors();
    set({ status: "starting", errorMsg: "", graphName });
    try {
      await startEngine(graphName);
      set({ status: "running" });
    } catch (err: unknown) {
      const detail =
        (err as { response?: { data?: { detail?: unknown } } })?.response?.data?.detail;

      // Try to extract per-node errors from validation_errors array
      const nodeErrors: Record<string, string> = {};
      if (detail && typeof detail === "object" && "validation_errors" in detail) {
        const ve = (detail as { validation_errors: string[] }).validation_errors;
        for (const msg of ve) {
          // Messages formatted as "node <id>: <reason>" or "Node <id>: ..."
          const m = msg.match(/^[Nn]ode\s+(\S+):\s+(.+)/);
          if (m) nodeErrors[m[1]] = m[2];
        }
      }
      if (Object.keys(nodeErrors).length > 0) {
        useGraphStore.getState().setNodeErrors(nodeErrors);
      }

      const msg = typeof detail === "string" ? detail : JSON.stringify(detail ?? err);
      set({ status: "error", errorMsg: msg });
    }
  },

  stop: async () => {
    try {
      await stopEngine();
    } catch {
      /* ignore */
    }
    useGraphStore.getState().clearNodeErrors();
    set({ status: "stopped", stats: null });
  },
}));

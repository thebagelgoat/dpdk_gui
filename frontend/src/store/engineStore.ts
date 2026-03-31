import { create } from "zustand";
import type { StatsMessage } from "../types/stats";
import { startEngine, stopEngine } from "../api/engine";

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
    set({ status: "starting", errorMsg: "", graphName });
    try {
      await startEngine(graphName);
      set({ status: "running" });
    } catch (err: unknown) {
      const detail =
        (err as { response?: { data?: { detail?: string } } })?.response?.data?.detail;
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
    set({ status: "stopped", stats: null });
  },
}));

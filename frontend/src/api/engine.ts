import api from "./client";

export const startEngine = (graphName: string) =>
  api.post("/engine/start", { graph_name: graphName }).then((r) => r.data);

export const stopEngine = () => api.post("/engine/stop").then((r) => r.data);

export const getEngineStatus = () =>
  api.get<{ state: string; graph_name: string; pid: number | null }>("/engine/status").then((r) => r.data);

export const getSystemInfo = () => api.get("/system/").then((r) => r.data);

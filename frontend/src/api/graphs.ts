import api from "./client";
import type { GraphSchema } from "../types/graph";

export const listGraphs = () => api.get<string[]>("/graphs/").then((r) => r.data);
export const getGraph = (name: string) => api.get<GraphSchema>(`/graphs/${name}`).then((r) => r.data);
export const saveGraph = (name: string, graph: GraphSchema) =>
  api.post(`/graphs/${name}`, graph).then((r) => r.data);
export const deleteGraph = (name: string) => api.delete(`/graphs/${name}`).then((r) => r.data);

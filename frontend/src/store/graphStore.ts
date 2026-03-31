import { create } from "zustand";
import { applyNodeChanges, applyEdgeChanges } from "reactflow";
import type { Node, Edge, NodeChange, EdgeChange, Connection } from "reactflow";
import type { GraphSchema, ModuleType } from "../types/graph";
import { MODULE_DEFS } from "../types/graph";

const MAX_HISTORY = 50;

interface GraphState {
  nodes: Node[];
  edges: Edge[];
}

interface GraphStore {
  nodes: Node[];
  edges: Edge[];
  graphName: string;
  isDirty: boolean;
  nodeErrors: Record<string, string>;
  setNodes: (nodes: Node[]) => void;
  setEdges: (edges: Edge[]) => void;
  onNodesChange: (changes: NodeChange[]) => void;
  onEdgesChange: (changes: EdgeChange[]) => void;
  onConnect: (connection: Connection) => void;
  addNode: (type: ModuleType, position: { x: number; y: number }) => void;
  updateNodeConfig: (nodeId: string, config: Record<string, unknown>) => void;
  updateNodeLabel: (nodeId: string, label: string) => void;
  setGraphName: (name: string) => void;
  markSaved: () => void;
  setNodeErrors: (errors: Record<string, string>) => void;
  clearNodeErrors: () => void;
  loadGraph: (schema: GraphSchema) => void;
  toGraphSchema: () => GraphSchema;
  clearGraph: () => void;
  undo: () => void;
  redo: () => void;
  canUndo: () => boolean;
  canRedo: () => boolean;
}

let nodeCounter = 0;
const newId = () => `node_${Date.now()}_${nodeCounter++}`;
const newEdgeId = () => `edge_${Date.now()}_${nodeCounter++}`;

// History stacks live outside the store to avoid triggering rerenders
let _history: GraphState[] = [];
let _future: GraphState[] = [];

function pushHistory(nodes: Node[], edges: Edge[]) {
  _history.push({ nodes, edges });
  if (_history.length > MAX_HISTORY) _history.shift();
  _future = [];
}

export const useGraphStore = create<GraphStore>((set, get) => ({
  nodes: [],
  edges: [],
  graphName: "untitled",
  isDirty: false,
  nodeErrors: {},

  setNodes: (nodes) => {
    pushHistory(get().nodes, get().edges);
    set({ nodes, isDirty: true });
  },
  setEdges: (edges) => {
    pushHistory(get().nodes, get().edges);
    set({ edges, isDirty: true });
  },

  onNodesChange: (changes) => {
    // Only push history for significant changes (not selection/position drags)
    const significant = changes.some((c) => c.type === "remove");
    if (significant) pushHistory(get().nodes, get().edges);
    set((s) => ({ nodes: applyNodeChanges(changes, s.nodes), isDirty: true }));
  },

  onEdgesChange: (changes) => {
    const significant = changes.some((c) => c.type === "remove");
    if (significant) pushHistory(get().nodes, get().edges);
    set((s) => ({ edges: applyEdgeChanges(changes, s.edges), isDirty: true }));
  },

  onConnect: (connection) => {
    if (!connection.source || !connection.target) return;
    pushHistory(get().nodes, get().edges);
    const edge: Edge = {
      id: newEdgeId(),
      source: connection.source,
      sourceHandle: connection.sourceHandle ?? undefined,
      target: connection.target,
      targetHandle: connection.targetHandle ?? undefined,
      type: "animated",
      data: { ring: { name: "", size: 1024 } },
    };
    set((s) => ({ edges: [...s.edges, edge], isDirty: true }));
  },

  addNode: (type, position) => {
    const def = MODULE_DEFS.find((d) => d.type === type);
    if (!def) return;
    pushHistory(get().nodes, get().edges);
    const id = newId();
    const node: Node = {
      id,
      type: "module",
      position,
      data: {
        label: def.label,
        moduleType: type,
        config: { ...def.defaultConfig },
        color: def.color,
        maxInputs: def.maxInputs,
        maxOutputs: def.maxOutputs,
      },
    };
    set((s) => ({ nodes: [...s.nodes, node], isDirty: true }));
  },

  updateNodeConfig: (nodeId, config) => {
    pushHistory(get().nodes, get().edges);
    set((s) => ({
      nodes: s.nodes.map((n) =>
        n.id === nodeId ? { ...n, data: { ...n.data, config } } : n
      ),
      isDirty: true,
    }));
  },

  updateNodeLabel: (nodeId, label) =>
    set((s) => ({
      nodes: s.nodes.map((n) =>
        n.id === nodeId ? { ...n, data: { ...n.data, label } } : n
      ),
      isDirty: true,
    })),

  setGraphName: (name) => set({ graphName: name }),

  markSaved: () => set({ isDirty: false }),

  setNodeErrors: (errors) => set({ nodeErrors: errors }),
  clearNodeErrors: () => set({ nodeErrors: {} }),

  loadGraph: (schema) => {
    _history = [];
    _future = [];
    const nodes: Node[] = schema.graph.nodes.map((n) => {
      const def = MODULE_DEFS.find((d) => d.type === n.type);
      return {
        id: n.id,
        type: "module",
        position: n.position,
        data: {
          label: n.label || n.type,
          moduleType: n.type,
          config: n.config,
          color: def?.color ?? "#374151",
          maxInputs: def?.maxInputs ?? 1,
          maxOutputs: def?.maxOutputs ?? 1,
        },
      };
    });

    const edges: Edge[] = schema.graph.edges.map((e) => ({
      id: e.id,
      source: e.source,
      sourceHandle: `out-${e.source_port}`,
      target: e.target,
      targetHandle: `in-${e.target_port}`,
      type: "animated",
      data: { ring: e.ring },
    }));

    set({ nodes, edges, graphName: schema.name, isDirty: false, nodeErrors: {} });
  },

  toGraphSchema: (): GraphSchema => {
    const { nodes, edges, graphName } = get();
    return {
      version: "1",
      name: graphName,
      graph: {
        nodes: nodes.map((n) => ({
          id: n.id,
          type: n.data.moduleType as ModuleType,
          label: n.data.label as string,
          core: 1,
          config: (n.data.config as Record<string, unknown>) ?? {},
          position: n.position,
        })),
        edges: edges.map((e) => {
          const srcPort = parseInt((e.sourceHandle ?? "out-0").replace("out-", "")) || 0;
          const tgtPort = parseInt((e.targetHandle ?? "in-0").replace("in-", "")) || 0;
          return {
            id: e.id,
            source: e.source,
            source_port: srcPort,
            target: e.target,
            target_port: tgtPort,
            ring: (e.data as { ring: { name: string; size: number } })?.ring ?? { name: "", size: 1024 },
          };
        }),
      },
    };
  },

  clearGraph: () => {
    _history = [];
    _future = [];
    set({ nodes: [], edges: [], isDirty: false, nodeErrors: {} });
  },

  undo: () => {
    const prev = _history.pop();
    if (!prev) return;
    _future.push({ nodes: get().nodes, edges: get().edges });
    set({ nodes: prev.nodes, edges: prev.edges, isDirty: true });
  },

  redo: () => {
    const next = _future.pop();
    if (!next) return;
    _history.push({ nodes: get().nodes, edges: get().edges });
    set({ nodes: next.nodes, edges: next.edges, isDirty: true });
  },

  canUndo: () => _history.length > 0,
  canRedo: () => _future.length > 0,
}));

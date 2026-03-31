import { create } from "zustand";

interface UIStore {
  selectedNodeId: string | null;
  configPanelOpen: boolean;
  graphListOpen: boolean;
  setSelectedNode: (id: string | null) => void;
  setConfigPanelOpen: (open: boolean) => void;
  setGraphListOpen: (open: boolean) => void;
}

export const useUIStore = create<UIStore>((set) => ({
  selectedNodeId: null,
  configPanelOpen: false,
  graphListOpen: false,
  setSelectedNode: (id) => set({ selectedNodeId: id, configPanelOpen: id !== null }),
  setConfigPanelOpen: (open) => set({ configPanelOpen: open }),
  setGraphListOpen: (open) => set({ graphListOpen: open }),
}));

export type ModuleType =
  | "nic_rx" | "nic_tx"
  | "ip_filter" | "vlan_filter" | "port_filter"
  | "pcap_recorder" | "counter" | "template";

export interface RingConfig {
  name: string;
  size: number;
}

export interface NodePosition {
  x: number;
  y: number;
}

export interface GraphNode {
  id: string;
  type: ModuleType;
  label: string;
  core: number;
  config: Record<string, unknown>;
  position: NodePosition;
}

export interface GraphEdge {
  id: string;
  source: string;
  source_port: number;
  target: string;
  target_port: number;
  ring: RingConfig;
}

export interface GraphBody {
  nodes: GraphNode[];
  edges: GraphEdge[];
}

export interface GraphSchema {
  version: string;
  name: string;
  graph: GraphBody;
}

export interface ModuleDefinition {
  type: ModuleType;
  label: string;
  description: string;
  color: string;
  defaultConfig: Record<string, unknown>;
  maxInputs: number;
  maxOutputs: number;
}

export const MODULE_DEFS: ModuleDefinition[] = [
  {
    type: "nic_rx",
    label: "NIC RX",
    description: "Receive packets from a NIC port",
    color: "#2563eb",
    defaultConfig: { port_id: 0, queue_id: 0, burst_size: 32, output_mode: "first" },
    maxInputs: 0,
    maxOutputs: 4,
  },
  {
    type: "nic_tx",
    label: "NIC TX",
    description: "Transmit packets to a NIC port",
    color: "#7c3aed",
    defaultConfig: { port_id: 1, queue_id: 0 },
    maxInputs: 1,
    maxOutputs: 0,
  },
  {
    type: "ip_filter",
    label: "IP Filter",
    description: "Filter packets by IPv4 CIDR",
    color: "#d97706",
    defaultConfig: {
      rules: [{ src_cidr: "0.0.0.0/0", dst_cidr: "0.0.0.0/0", action: "pass" }],
      default_action: "drop",
      output_mode: "first",
    },
    maxInputs: 1,
    maxOutputs: 4,
  },
  {
    type: "vlan_filter",
    label: "VLAN Filter",
    description: "Filter packets by VLAN ID",
    color: "#d97706",
    defaultConfig: { vlan_ids: [100], action: "pass", strip_tag: false, output_mode: "first" },
    maxInputs: 1,
    maxOutputs: 4,
  },
  {
    type: "port_filter",
    label: "Port Filter",
    description: "Filter packets by TCP/UDP port",
    color: "#d97706",
    defaultConfig: { protocol: "both", ports: [80, 443], action: "pass", output_mode: "first" },
    maxInputs: 1,
    maxOutputs: 4,
  },
  {
    type: "pcap_recorder",
    label: "PCAP Recorder",
    description: "Capture packets to a .pcap file",
    color: "#dc2626",
    defaultConfig: { output_path: "/tmp/capture.pcap", max_file_size_mb: 512, snaplen: 65535, output_mode: "first" },
    maxInputs: 1,
    maxOutputs: 4,
  },
  {
    type: "counter",
    label: "Counter",
    description: "Count packets and bytes",
    color: "#6b7280",
    defaultConfig: { label: "counter", reset_on_read: false, output_mode: "first" },
    maxInputs: 1,
    maxOutputs: 4,
  },
  {
    type: "template",
    label: "Template",
    description: "Custom module template",
    color: "#374151",
    defaultConfig: { user_label: "custom", pass_through: true, output_mode: "first" },
    maxInputs: 1,
    maxOutputs: 4,
  },
];

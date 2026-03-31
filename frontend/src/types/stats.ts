export interface NodeStats {
  id: string;
  pkts_processed: number;
  pkts_dropped: number;
  bytes_processed: number;
  core_id: number;
  pps: number;
  bps: number;
  rule_hits?: number[];
}

export interface RingStats {
  name: string;
  capacity: number;
  used: number;
  fill_pct: number;
  peak_fill_pct: number;
}

export interface StatsMessage {
  type: string;
  timestamp: number;
  nodes: NodeStats[];
  rings: RingStats[];
  lcore_util: number[];
  cpu_count?: number;
}

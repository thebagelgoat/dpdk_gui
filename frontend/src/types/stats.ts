export interface InspectorSample {
  ts_us: number;
  src_ip: number;   /* raw uint32 host byte order — format with formatIP() */
  dst_ip: number;
  src_port: number;
  dst_port: number;
  eth_type: number;
  pkt_len: number;
  ip_proto: number;
  tcp_flags: number;
}

export interface NodeStats {
  id: string;
  pkts_processed: number;
  pkts_dropped: number;
  bytes_processed: number;
  core_id: number;
  pps: number;
  bps: number;
  rule_hits?: number[];
  samples?: InspectorSample[];
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

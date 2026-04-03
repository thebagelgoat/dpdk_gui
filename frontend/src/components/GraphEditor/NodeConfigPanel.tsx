import React, { useState, useEffect, useCallback, useRef } from "react";
import { useGraphStore } from "../../store/graphStore";
import { useUIStore } from "../../store/uiStore";
import { useEngineStore } from "../../store/engineStore";
import { reloadNodeConfig } from "../../api/engine";
import { InspectorSample } from "../../types/stats";

// ─── Field schema ───────────────────────────────────────────────────────────

type FieldDef =
  | { type: "number"; key: string; label: string; min?: number; max?: number; step?: number }
  | { type: "select"; key: string; label: string; options: string[] }
  | { type: "toggle"; key: string; label: string }
  | { type: "text";   key: string; label: string; placeholder?: string }
  | { type: "number-list"; key: string; label: string; min?: number; max?: number }
  | { type: "ip-rules"; key: string; label: string };

// Appended to any node that has maxOutputs > 1
const OUTPUT_MODE_FIELDS: FieldDef[] = [
  { type: "select", key: "output_mode", label: "Output Mode",
    options: ["first", "duplicate", "load_balance"] },
  { type: "select", key: "lb_mode", label: "LB Mode (if load_balance)",
    options: ["round_robin", "rss"] },
];

const CONFIG_SCHEMA: Record<string, FieldDef[]> = {
  nic_rx: [
    { type: "number", key: "port_id",    label: "Port ID",    min: 0, max: 7 },
    { type: "number", key: "queue_id",   label: "Queue ID",   min: 0, max: 15 },
    { type: "number", key: "burst_size", label: "Burst Size", min: 1, max: 256, step: 8 },
    ...OUTPUT_MODE_FIELDS,
  ],
  nic_tx: [
    { type: "number", key: "port_id",  label: "Port ID",  min: 0, max: 7 },
    { type: "number", key: "queue_id", label: "Queue ID", min: 0, max: 15 },
  ],
  ip_filter: [
    { type: "ip-rules", key: "rules",          label: "Rules" },
    { type: "select",   key: "default_action", label: "Default Action", options: ["pass", "drop"] },
    ...OUTPUT_MODE_FIELDS,
  ],
  vlan_filter: [
    { type: "number-list", key: "vlan_ids",   label: "VLAN IDs (comma-separated)", min: 0, max: 4094 },
    { type: "select",      key: "action",     label: "Action",    options: ["pass", "drop"] },
    { type: "toggle",      key: "strip_tag",  label: "Strip VLAN tag" },
    ...OUTPUT_MODE_FIELDS,
  ],
  port_filter: [
    { type: "select",      key: "protocol", label: "Protocol", options: ["tcp", "udp", "both"] },
    { type: "number-list", key: "ports",    label: "Ports (comma-separated)", min: 1, max: 65535 },
    { type: "select",      key: "action",   label: "Action",   options: ["pass", "drop"] },
    ...OUTPUT_MODE_FIELDS,
  ],
  protocol_filter: [
    { type: "number-list", key: "ip_protos",  label: "IP Protocol numbers (e.g. 17=UDP, 6=TCP, 1=ICMP)" },
    { type: "number-list", key: "ethertypes", label: "EtherTypes as integers (e.g. 2048=IPv4, 34525=IPv6)" },
    { type: "select",      key: "action",         label: "Action on match",  options: ["pass", "drop"] },
    { type: "select",      key: "default_action", label: "Default action",   options: ["pass", "drop"] },
    ...OUTPUT_MODE_FIELDS,
  ],
  mac_filter: [
    { type: "text",   key: "addresses",      label: "MAC addresses (comma-separated)",
      placeholder: "aa:bb:cc:dd:ee:ff, 11:22:33:44:55:66" },
    { type: "select", key: "match_field",    label: "Match field",      options: ["src", "dst", "either"] },
    { type: "select", key: "action",         label: "Action on match",  options: ["pass", "drop"] },
    { type: "select", key: "default_action", label: "Default action",   options: ["pass", "drop"] },
    ...OUTPUT_MODE_FIELDS,
  ],
  pcap_recorder: [
    { type: "text",   key: "output_path",      label: "Output Path", placeholder: "/tmp/capture.pcap" },
    { type: "number", key: "max_file_size_mb", label: "Max File Size (MB)", min: 0, max: 65536 },
    { type: "number", key: "snaplen",          label: "Snap Length",         min: 64, max: 65535 },
    ...OUTPUT_MODE_FIELDS,
  ],
  speedometer: [
    { type: "text",   key: "label",         label: "Label", placeholder: "speedometer" },
    { type: "toggle", key: "reset_on_read", label: "Reset on read" },
    ...OUTPUT_MODE_FIELDS,
  ],
  template: [
    { type: "text",   key: "user_label",   label: "Label",       placeholder: "custom" },
    { type: "toggle", key: "pass_through", label: "Pass-through" },
    ...OUTPUT_MODE_FIELDS,
  ],
  pcap_source: [
    { type: "text",   key: "file_path",        label: "PCAP File Path",              placeholder: "/tmp/capture.pcap" },
    { type: "number", key: "speed_multiplier",  label: "Speed Multiplier (0=full)",   min: 0, max: 1000, step: 0.1 },
    { type: "toggle", key: "loop",              label: "Loop Playback" },
    ...OUTPUT_MODE_FIELDS,
  ],
  pkt_gen: [
    { type: "number", key: "rate_pps",      label: "Rate PPS (0=unlimited)",  min: 0, max: 100000000 },
    { type: "text",   key: "src_ip",        label: "Source IP (CIDR)",        placeholder: "10.0.0.1/32" },
    { type: "text",   key: "dst_ip",        label: "Dest IP (CIDR)",          placeholder: "192.168.1.1/32" },
    { type: "text",   key: "src_mac",       label: "Source MAC",              placeholder: "02:00:00:00:00:01" },
    { type: "text",   key: "dst_mac",       label: "Dest MAC",                placeholder: "ff:ff:ff:ff:ff:ff" },
    { type: "select", key: "protocol",      label: "Protocol",                options: ["udp", "tcp", "icmp"] },
    { type: "number", key: "src_port_min",  label: "Src Port Min",            min: 1, max: 65535 },
    { type: "number", key: "src_port_max",  label: "Src Port Max",            min: 1, max: 65535 },
    { type: "number", key: "dst_port_min",  label: "Dst Port Min",            min: 1, max: 65535 },
    { type: "number", key: "dst_port_max",  label: "Dst Port Max",            min: 1, max: 65535 },
    { type: "number", key: "pkt_size",      label: "Packet Size (bytes)",     min: 60, max: 1514 },
    ...OUTPUT_MODE_FIELDS,
  ],
  packet_inspector: [
    { type: "number", key: "sample_every_n", label: "Sample every Nth packet", min: 1, max: 1000 },
    ...OUTPUT_MODE_FIELDS,
  ],
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

const formatIP = (n: number): string =>
  `${(n >>> 24) & 0xff}.${(n >>> 16) & 0xff}.${(n >>> 8) & 0xff}.${n & 0xff}`;

const PROTO_LABELS: Record<number, { label: string; color: string }> = {
  1:  { label: "ICMP",  color: "#f59e0b" },
  6:  { label: "TCP",   color: "#3b82f6" },
  17: { label: "UDP",   color: "#22c55e" },
};

const ETH_LABELS: Record<number, string> = {
  0x0800: "IPv4",
  0x86DD: "IPv6",
  0x0806: "ARP",
  0x8100: "VLAN",
};

const TCP_FLAG_NAMES = ["FIN","SYN","RST","PSH","ACK","URG"];
const formatTcpFlags = (f: number): string =>
  TCP_FLAG_NAMES.filter((_, i) => f & (1 << i)).join("+") || "—";

function InspectorTable({ samples }: { samples: InspectorSample[] }) {
  /* Show newest first, cap at 100 rows */
  const rows = [...samples].reverse().slice(0, 100);
  if (rows.length === 0) {
    return (
      <div style={{ fontSize: 11, color: "#475569", fontStyle: "italic", padding: "8px 0" }}>
        No packets captured yet…
      </div>
    );
  }
  const firstTs = rows[rows.length - 1]?.ts_us ?? 0;
  return (
    <div style={{ overflowX: "auto" }}>
      <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 10 }}>
        <thead>
          <tr style={{ color: "#475569", textAlign: "left" }}>
            <th style={thStyle}>Time (ms)</th>
            <th style={thStyle}>Proto</th>
            <th style={thStyle}>Source</th>
            <th style={thStyle}>Destination</th>
            <th style={thStyle}>Len</th>
            <th style={thStyle}>Info</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((s, i) => {
            const protoInfo = PROTO_LABELS[s.ip_proto];
            const isIP = s.eth_type === 0x0800 || s.eth_type === 0x86DD;
            const proto = isIP
              ? (protoInfo ? protoInfo.label : `IP/${s.ip_proto}`)
              : (ETH_LABELS[s.eth_type] ?? `0x${s.eth_type.toString(16)}`);
            const protoColor = protoInfo?.color ?? "#94a3b8";
            const elapsed = ((s.ts_us - firstTs) / 1000).toFixed(1);
            const src = isIP
              ? `${formatIP(s.src_ip)}${s.src_port ? `:${s.src_port}` : ""}`
              : "—";
            const dst = isIP
              ? `${formatIP(s.dst_ip)}${s.dst_port ? `:${s.dst_port}` : ""}`
              : "—";
            const info = s.ip_proto === 6 ? formatTcpFlags(s.tcp_flags) : "—";
            return (
              <tr key={i} style={{ borderBottom: "1px solid #13151f" }}>
                <td style={tdStyle}>{elapsed}</td>
                <td style={tdStyle}>
                  <span style={{
                    background: protoColor + "22",
                    color: protoColor,
                    borderRadius: 3,
                    padding: "1px 4px",
                    fontWeight: 700,
                    fontSize: 9,
                  }}>
                    {proto}
                  </span>
                </td>
                <td style={{ ...tdStyle, fontFamily: "monospace" }}>{src}</td>
                <td style={{ ...tdStyle, fontFamily: "monospace" }}>{dst}</td>
                <td style={tdStyle}>{s.pkt_len}</td>
                <td style={{ ...tdStyle, color: "#64748b" }}>{info}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}

const thStyle: React.CSSProperties = {
  padding: "3px 4px",
  borderBottom: "1px solid #2d3748",
  fontWeight: 700,
  fontSize: 9,
  textTransform: "uppercase",
  letterSpacing: 0.5,
  whiteSpace: "nowrap",
};

const tdStyle: React.CSSProperties = {
  padding: "3px 4px",
  color: "#cbd5e0",
  whiteSpace: "nowrap",
  maxWidth: 90,
  overflow: "hidden",
  textOverflow: "ellipsis",
};

// ─── Sub-components ──────────────────────────────────────────────────────────

function IpRuleEditor({
  rules,
  onChange,
  hits,
}: {
  rules: { src_cidr: string; dst_cidr: string; action: string }[];
  onChange: (r: typeof rules) => void;
  hits?: number[];
}) {
  const update = (idx: number, key: string, val: string) => {
    const next = rules.map((r, i) => (i === idx ? { ...r, [key]: val } : r));
    onChange(next);
  };
  const add = () => onChange([...rules, { src_cidr: "0.0.0.0/0", dst_cidr: "0.0.0.0/0", action: "pass" }]);
  const remove = (idx: number) => onChange(rules.filter((_, i) => i !== idx));

  return (
    <div>
      {rules.map((r, i) => (
        <div
          key={i}
          style={{
            background: "#131620",
            border: "1px solid #2d3748",
            borderRadius: 4,
            padding: "6px 8px",
            marginBottom: 6,
          }}
        >
          <div style={{ display: "flex", gap: 4, marginBottom: 4, alignItems: "center" }}>
            <span style={{ fontSize: 10, color: "#475569", width: 28 }}>Src</span>
            <input
              value={r.src_cidr}
              onChange={(e) => update(i, "src_cidr", e.target.value)}
              placeholder="0.0.0.0/0"
              style={{ ...inputStyle, flex: 1 }}
            />
          </div>
          <div style={{ display: "flex", gap: 4, marginBottom: 4, alignItems: "center" }}>
            <span style={{ fontSize: 10, color: "#475569", width: 28 }}>Dst</span>
            <input
              value={r.dst_cidr}
              onChange={(e) => update(i, "dst_cidr", e.target.value)}
              placeholder="0.0.0.0/0"
              style={{ ...inputStyle, flex: 1 }}
            />
          </div>
          <div style={{ display: "flex", gap: 4, alignItems: "center" }}>
            <span style={{ fontSize: 10, color: "#475569", width: 28 }}>Act</span>
            <select
              value={r.action}
              onChange={(e) => update(i, "action", e.target.value)}
              style={{ ...inputStyle, flex: 1 }}
            >
              <option value="pass">pass</option>
              <option value="drop">drop</option>
            </select>
            {hits && hits[i] !== undefined && (
              <span style={{ fontSize: 10, color: "#34d399", whiteSpace: "nowrap", minWidth: 40, textAlign: "right" }}>
                {hits[i].toLocaleString()}
              </span>
            )}
            <button
              onClick={() => remove(i)}
              style={{ background: "#3f1c1c", border: "1px solid #7f1d1d", borderRadius: 3, color: "#f87171", cursor: "pointer", padding: "2px 7px", fontSize: 12 }}
            >
              ×
            </button>
          </div>
        </div>
      ))}
      <button onClick={add} style={{ ...btnSecondary, width: "100%" }}>
        + Add Rule
      </button>
    </div>
  );
}

// ─── Number-list input (local string state so commas can be typed freely) ────

function NumberListInput({
  value,
  onChange,
  placeholder = "e.g. 80, 443",
  style,
}: {
  value: number[];
  onChange: (v: number[]) => void;
  placeholder?: string;
  style?: React.CSSProperties;
}) {
  const [text, setText] = useState(value.join(", "));

  // If the external value changes (e.g. node selection changes), sync local text
  const prevValueRef = useRef(value);
  useEffect(() => {
    if (prevValueRef.current !== value) {
      prevValueRef.current = value;
      setText(value.join(", "));
    }
  }, [value]);

  return (
    <input
      type="text"
      value={text}
      placeholder={placeholder}
      style={style}
      onChange={(e) => setText(e.target.value)}
      onBlur={() => {
        const parsed = text
          .split(",")
          .map((s) => parseInt(s.trim(), 10))
          .filter((n) => !isNaN(n));
        onChange(parsed);
        setText(parsed.join(", "));
      }}
    />
  );
}

// ─── Main panel ──────────────────────────────────────────────────────────────

export default function NodeConfigPanel() {
  const { selectedNodeId, setSelectedNode } = useUIStore();
  const { nodes, updateNodeConfig, updateNodeLabel } = useGraphStore();
  const engineStats = useEngineStore((s) => s.stats);
  const isRunning = useEngineStore((s) => s.status === "running");

  const [panelWidth, setPanelWidth] = useState(290);
  const dragStart = useRef<{ x: number; w: number } | null>(null);

  const onResizeMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    dragStart.current = { x: e.clientX, w: panelWidth };
    const onMove = (ev: MouseEvent) => {
      if (!dragStart.current) return;
      const delta = dragStart.current.x - ev.clientX;
      setPanelWidth(Math.max(220, Math.min(600, dragStart.current.w + delta)));
    };
    const onUp = () => {
      dragStart.current = null;
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [panelWidth]);

  const node = nodes.find((n) => n.id === selectedNodeId);
  const moduleType = node?.data.moduleType as string;
  const schema = CONFIG_SCHEMA[moduleType] ?? [];

  // Rule hit counts for this node (from live stats)
  const nodeStats = engineStats?.nodes.find((n) => n.id === selectedNodeId);
  const ruleHits = nodeStats?.rule_hits;

  const [fields, setFields] = useState<Record<string, unknown>>({});
  const [label, setLabel] = useState("");
  const [showRaw, setShowRaw] = useState(false);
  const [rawText, setRawText] = useState("");
  const [error, setError] = useState("");
  const [liveMsg, setLiveMsg] = useState("");

  useEffect(() => {
    if (node) {
      const cfg = (node.data.config ?? {}) as Record<string, unknown>;
      setFields(cfg);
      setLabel(node.data.label as string);
      setRawText(JSON.stringify(cfg, null, 2));
      setError("");
    }
  }, [selectedNodeId, node]);

  if (!node) return null;

  const setField = (key: string, value: unknown) => {
    setFields((prev) => {
      const next = { ...prev, [key]: value };
      setRawText(JSON.stringify(next, null, 2));
      return next;
    });
  };

  const handleRawChange = (text: string) => {
    setRawText(text);
    try {
      setFields(JSON.parse(text));
      setError("");
    } catch {
      setError("Invalid JSON");
    }
  };

  const handleApply = () => {
    try {
      const cfg = showRaw ? JSON.parse(rawText) : fields;
      updateNodeConfig(node.id, cfg);
      updateNodeLabel(node.id, label);
      setError("");
    } catch (e) {
      setError("Invalid JSON: " + (e as Error).message);
    }
  };

  const handleApplyLive = async () => {
    try {
      const cfg = showRaw ? JSON.parse(rawText) : fields;
      updateNodeConfig(node.id, cfg);
      updateNodeLabel(node.id, label);
      await reloadNodeConfig(node.id, cfg);
      setLiveMsg("Live!");
      setTimeout(() => setLiveMsg(""), 2000);
    } catch (e) {
      setLiveMsg("Failed");
      setTimeout(() => setLiveMsg(""), 3000);
    }
  };

  const renderField = (f: FieldDef) => {
    const val = fields[f.key];

    if (f.type === "number") {
      return (
        <input
          type="number"
          value={val as number ?? 0}
          min={f.min}
          max={f.max}
          step={f.step ?? 1}
          onChange={(e) => setField(f.key, Number(e.target.value))}
          style={inputStyle}
        />
      );
    }
    if (f.type === "select") {
      return (
        <select
          value={val as string ?? f.options[0]}
          onChange={(e) => setField(f.key, e.target.value)}
          style={inputStyle}
        >
          {f.options.map((o) => <option key={o} value={o}>{o}</option>)}
        </select>
      );
    }
    if (f.type === "toggle") {
      return (
        <label style={{ display: "flex", alignItems: "center", gap: 8, cursor: "pointer" }}>
          <input
            type="checkbox"
            checked={val as boolean ?? false}
            onChange={(e) => setField(f.key, e.target.checked)}
            style={{ accentColor: "#2563eb", width: 14, height: 14 }}
          />
          <span style={{ fontSize: 12, color: "#94a3b8" }}>{val ? "Enabled" : "Disabled"}</span>
        </label>
      );
    }
    if (f.type === "text") {
      // Arrays (e.g. protocols) are stored as arrays but edited as comma strings
      const strVal = Array.isArray(val) ? (val as string[]).join(", ") : (val as string ?? "");
      return (
        <input
          type="text"
          value={strVal}
          placeholder={(f as any).placeholder}
          onChange={(e) => {
            const raw = e.target.value;
            // If the original value was an array, keep it as an array
            if (Array.isArray(val) || f.key === "protocols" || f.key === "addresses") {
              setField(f.key, raw.split(",").map((s) => s.trim()).filter(Boolean));
            } else {
              setField(f.key, raw);
            }
          }}
          style={inputStyle}
        />
      );
    }
    if (f.type === "number-list") {
      const list = (val as number[] | undefined) ?? [];
      return (
        <NumberListInput
          value={list}
          onChange={(parsed) => setField(f.key, parsed)}
          style={inputStyle}
        />
      );
    }
    if (f.type === "ip-rules") {
      const rules = (val as any[]) ?? [];
      return (
        <>
          <IpRuleEditor rules={rules} onChange={(r) => setField(f.key, r)} hits={ruleHits} />
          {ruleHits && ruleHits.length > rules.length && (
            <div style={{ fontSize: 10, color: "#64748b", marginTop: 4 }}>
              Default action hits: {ruleHits[rules.length].toLocaleString()}
            </div>
          )}
        </>
      );
    }
    return null;
  };

  return (
    <div
      style={{
        width: panelWidth,
        background: "#0b0d18",
        borderLeft: "1px solid #2d3748",
        display: "flex",
        flexDirection: "row",
        flexShrink: 0,
        overflow: "hidden",
      }}
    >
      {/* Drag handle — left edge */}
      <div
        onMouseDown={onResizeMouseDown}
        style={{
          width: 5,
          cursor: "ew-resize",
          background: "transparent",
          flexShrink: 0,
          transition: "background 0.15s",
        }}
        onMouseEnter={(e) => { (e.currentTarget as HTMLDivElement).style.background = "#2d3748"; }}
        onMouseLeave={(e) => { (e.currentTarget as HTMLDivElement).style.background = "transparent"; }}
        title="Drag to resize"
      />
      {/* Panel content */}
      <div style={{ flex: 1, display: "flex", flexDirection: "column", overflow: "hidden", minWidth: 0 }}>
      {/* Header */}
      <div
        style={{
          padding: "10px 12px",
          borderBottom: "1px solid #2d3748",
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          background: "#0a0c14",
        }}
      >
        <div style={{ fontSize: 11, fontWeight: 700, textTransform: "uppercase", color: "#64748b", letterSpacing: 1 }}>
          Configure Node
        </div>
        <button
          onClick={() => setSelectedNode(null)}
          style={{ background: "none", border: "none", color: "#64748b", cursor: "pointer", fontSize: 16 }}
        >
          ×
        </button>
      </div>

      <div style={{ padding: 12, overflowY: "auto", flex: 1 }}>
        {/* Node type badge */}
        <div style={{ marginBottom: 14 }}>
          <div
            style={{
              display: "inline-block",
              background: node.data.color as string,
              color: "#fff",
              borderRadius: 4,
              padding: "2px 8px",
              fontSize: 11,
              fontWeight: 700,
              textTransform: "uppercase",
            }}
          >
            {moduleType}
          </div>
          <div style={{ fontSize: 10, color: "#475569", marginTop: 4 }}>ID: {node.id}</div>
        </div>

        {/* Label */}
        <div style={fieldGroup}>
          <label style={fieldLabel}>Label</label>
          <input
            value={label}
            onChange={(e) => setLabel(e.target.value)}
            style={inputStyle}
          />
        </div>

        <div style={{ borderTop: "1px solid #1e2435", margin: "12px 0" }} />

        {/* Form fields or raw JSON */}
        {!showRaw ? (
          schema.length === 0 ? (
            <div style={{ fontSize: 12, color: "#475569", fontStyle: "italic" }}>No configurable options.</div>
          ) : (
            schema.map((f) => (
              <div key={f.key} style={fieldGroup}>
                <label style={fieldLabel}>{f.label}</label>
                {renderField(f)}
              </div>
            ))
          )
        ) : (
          <div style={fieldGroup}>
            <label style={fieldLabel}>Raw JSON</label>
            <textarea
              value={rawText}
              onChange={(e) => handleRawChange(e.target.value)}
              rows={10}
              style={{ ...inputStyle, resize: "vertical", fontFamily: "monospace", fontSize: 11 }}
            />
          </div>
        )}

        {/* Match count for port_filter / protocol_filter */}
        {isRunning && ruleHits && ruleHits[0] !== undefined &&
          (moduleType === "port_filter" || moduleType === "protocol_filter") && (
          <div style={{ fontSize: 11, color: "#34d399", marginBottom: 8 }}>
            Matches: {ruleHits[0].toLocaleString()}
          </div>
        )}

        {error && <div style={{ color: "#f87171", fontSize: 11, marginBottom: 8 }}>{error}</div>}

        <div style={{ display: "flex", gap: 6 }}>
          <button onClick={handleApply} style={{ ...btnPrimary, flex: 1 }}>
            Apply
          </button>
          {isRunning && (
            <button
              onClick={handleApplyLive}
              style={{
                flex: 1,
                background: "#166534",
                border: "1px solid #15803d",
                borderRadius: 4,
                color: liveMsg === "Failed" ? "#f87171" : liveMsg ? "#4ade80" : "#86efac",
                padding: "8px",
                fontSize: 13,
                fontWeight: 600,
                cursor: "pointer",
              }}
            >
              {liveMsg || "Apply Live"}
            </button>
          )}
        </div>

        {/* Advanced toggle */}
        <div style={{ marginTop: 10, textAlign: "center" }}>
          <button
            onClick={() => setShowRaw((v) => !v)}
            style={{
              background: "none", border: "none",
              color: "#475569", fontSize: 11, cursor: "pointer",
              textDecoration: "underline",
            }}
          >
            {showRaw ? "Back to form" : "Advanced (raw JSON)"}
          </button>
        </div>

        {/* Live packet inspector table */}
        {moduleType === "packet_inspector" && isRunning && (
          <div style={{ marginTop: 14 }}>
            <div style={{
              fontSize: 9, fontWeight: 700, textTransform: "uppercase",
              letterSpacing: 1, color: "#475569", marginBottom: 6,
            }}>
              Live Packets
            </div>
            <InspectorTable samples={nodeStats?.samples ?? []} />
          </div>
        )}
      </div>
      </div>{/* end inner-content */}
    </div>
  );
}

// ─── Shared styles ────────────────────────────────────────────────────────────

const inputStyle: React.CSSProperties = {
  width: "100%",
  background: "#131620",
  border: "1px solid #2d3748",
  borderRadius: 4,
  color: "#e2e8f0",
  padding: "5px 8px",
  fontSize: 12,
  outline: "none",
  boxSizing: "border-box",
};

const btnPrimary: React.CSSProperties = {
  width: "100%",
  background: "#2563eb",
  border: "none",
  borderRadius: 4,
  color: "#fff",
  padding: "8px",
  fontSize: 13,
  fontWeight: 600,
  cursor: "pointer",
};

const btnSecondary: React.CSSProperties = {
  background: "#1e2435",
  border: "1px solid #374151",
  borderRadius: 4,
  color: "#94a3b8",
  padding: "5px 10px",
  fontSize: 12,
  cursor: "pointer",
};

const fieldGroup: React.CSSProperties = {
  marginBottom: 12,
};

const fieldLabel: React.CSSProperties = {
  fontSize: 11,
  color: "#64748b",
  display: "block",
  marginBottom: 4,
  fontWeight: 600,
  textTransform: "uppercase",
  letterSpacing: 0.5,
};

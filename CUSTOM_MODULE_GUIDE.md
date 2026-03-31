# Adding a Custom Module

This guide walks through every file you need to touch to add a new processing module — from the C engine through the Python backend to the React frontend.

As a concrete example we'll add a hypothetical `size_filter` module that passes or drops packets based on their byte length.

---

## 1. C Engine — the processing logic

### 1a. Create the module file

Create `engine/modules/size_filter.c`. Every module follows the same four-function pattern:

```c
#include "module_base.h"
#include "../node_out.h"   // required for multi-output support
#include <stdlib.h>
#include <string.h>

/* --- Config struct -------------------------------------------------------- */
typedef struct {
    uint32_t min_bytes;
    uint32_t max_bytes;   // 0 = no upper limit
} size_filter_cfg_t;

/* --- Parse config from graph JSON ---------------------------------------- */
static void *size_filter_parse(json_t *cfg) {
    size_filter_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    json_t *mn = json_object_get(cfg, "min_bytes");
    json_t *mx = json_object_get(cfg, "max_bytes");
    c->min_bytes = mn ? (uint32_t)json_integer_value(mn) : 0;
    c->max_bytes = mx ? (uint32_t)json_integer_value(mx) : 0;
    return c;
}

/* --- Hot-path processing -------------------------------------------------- */
static int size_filter_process(node_desc_t *node) {
    size_filter_cfg_t *c = node->module_cfg;
    if (!node->input_rings[0] || !node->input_rings[0]->ring) return 0;

    struct rte_mbuf *pkts[BURST_SIZE];
    unsigned n = rte_ring_dequeue_burst(node->input_rings[0]->ring,
                                         (void **)pkts, BURST_SIZE, NULL);
    if (n == 0) return 0;

    struct rte_mbuf *pass[BURST_SIZE];
    unsigned n_pass = 0;
    uint64_t bytes  = 0;

    for (unsigned i = 0; i < n; i++) {
        uint32_t len = pkts[i]->pkt_len;
        int ok = (len >= c->min_bytes) &&
                 (c->max_bytes == 0 || len <= c->max_bytes);
        if (ok) {
            bytes += len;
            pass[n_pass++] = pkts[i];
        } else {
            rte_pktmbuf_free(pkts[i]);
            atomic_fetch_add_explicit(&node->pkts_dropped, 1, memory_order_relaxed);
        }
    }

    if (n_pass > 0) {
        unsigned enq = node_out(node, pass, n_pass);  // handles output_mode
        atomic_fetch_add_explicit(&node->pkts_processed,  enq,   memory_order_relaxed);
        atomic_fetch_add_explicit(&node->bytes_processed, bytes, memory_order_relaxed);
    }
    return n;
}

/* --- Export the ops struct ------------------------------------------------ */
module_ops_t size_filter_ops = {
    .init         = NULL,           // set to a function if you need lcore init
    .process      = size_filter_process,
    .destroy      = NULL,           // set to a function to free resources on stop
    .parse_config = size_filter_parse,
};
```

**Key rules:**
- Always call `node_out(node, pass, n)` instead of calling `rte_ring_enqueue_burst` directly. This respects the node's `output_mode` (first / duplicate / load_balance).
- Always increment `node->pkts_dropped` for every packet you free without forwarding.
- Always update `node->pkts_processed` and `node->bytes_processed` with the count actually forwarded (the return value of `node_out`).
- `parse_config` must return a heap-allocated struct (or NULL on failure). It is called once at startup on the main thread.
- `process` is called in a tight loop on a DPDK lcore — no blocking, no syscalls, no printf in the hot path.

---

### 1b. Register the module type — `engine/graph.h`

Add your enum value to `module_type_t`:

```c
typedef enum {
    MOD_NIC_RX,
    // ... existing entries ...
    MOD_SIZE_FILTER,    // ← add here
    MOD_PCAP_RECORDER,
    MOD_COUNTER,
    MOD_TEMPLATE
} module_type_t;
```

---

### 1c. Wire it into the registry — `engine/graph.c`

Add to `module_registry[]`:

```c
module_ops_t *module_registry[] = {
    // ... existing entries ...
    [MOD_SIZE_FILTER] = &size_filter_ops,   // ← add here
    // ...
};
```

Add the string → enum mapping in `module_type_from_string()`:

```c
if (!strcmp(s, "size_filter")) return MOD_SIZE_FILTER;
```

---

### 1d. Declare the extern — `engine/modules/module_base.h`

```c
extern module_ops_t size_filter_ops;
```

---

### 1e. Add to the Makefile — `engine/Makefile`

```makefile
SRCS := main.c \
        # ... existing entries ...
        modules/size_filter.c \   # ← add here
        modules/template.c
```

---

### 1f. Rebuild

```bash
./build.sh
```

---

## 2. Python Backend

### 2a. Register the module type — `backend/models/graph_schema.py`

Add `"size_filter"` to the `ModuleType` Literal and to `HEAVY_TYPES` (or `LIGHTWEIGHT_TYPES` if it can safely share a core):

```python
ModuleType = Literal[
    # ... existing ...
    "size_filter",
    # ...
]

HEAVY_TYPES = {
    # ... existing ...
    "size_filter",
}
```

That's all the backend needs. The validator, core allocator, and engine manager all work generically.

---

## 3. React Frontend

### 3a. Add the TypeScript type — `frontend/src/types/graph.ts`

Add to the `ModuleType` union:

```typescript
export type ModuleType =
  | "nic_rx" | "nic_tx"
  | /* existing filters... */ | "size_filter"   // ← add here
  | "pcap_recorder" | "counter" | "template";
```

Add a `ModuleDefinition` entry to `MODULE_DEFS`:

```typescript
export const MODULE_DEFS: ModuleDefinition[] = [
  // ... existing entries ...
  {
    type: "size_filter",
    label: "Size Filter",
    description: "Pass or drop packets by byte length",
    color: "#d97706",           // pick any hex colour
    defaultConfig: {
      min_bytes: 64,
      max_bytes: 1500,
      output_mode: "first",     // always include output_mode
    },
    maxInputs: 1,
    maxOutputs: 4,              // set to 0 for sink nodes, >1 to allow fan-out
  },
];
```

**`maxInputs` / `maxOutputs` guide:**
| Node type | maxInputs | maxOutputs |
|-----------|-----------|------------|
| Source (nic_rx) | 0 | 4 |
| Sink (nic_tx) | 1 | 0 |
| Pass-through filter | 1 | 4 |
| Counter/monitor | 1 | 4 |

---

### 3b. Add to the palette — `frontend/src/components/GraphEditor/NodePalette.tsx`

Find the `CATEGORIES` array and add your module type to the appropriate category:

```typescript
const CATEGORIES: { label: string; types: ModuleType[] }[] = [
  { label: "I / O",     types: ["nic_rx", "nic_tx"] },
  { label: "Filters",   types: ["ip_filter", "vlan_filter", "port_filter",
                                 "protocol_filter", "mac_filter",
                                 "size_filter"] },   // ← add here
  { label: "Recording", types: ["pcap_recorder"] },
  { label: "Utility",   types: ["speedometer", "template"] },
];
```

---

### 3c. Add config fields — `frontend/src/components/GraphEditor/NodeConfigPanel.tsx`

Add an entry to `CONFIG_SCHEMA`. Available field types:

| Field type | Renders as | Notes |
|------------|------------|-------|
| `"number"` | `<input type="number">` | supports `min`, `max`, `step` |
| `"select"` | `<select>` | requires `options: string[]` |
| `"toggle"` | checkbox | stores boolean |
| `"text"` | `<input type="text">` | supports `placeholder` |
| `"number-list"` | text input | comma-separated integers → stored as `number[]` |
| `"ip-rules"` | rule list editor | stores `{src_cidr, dst_cidr, action}[]` |

```typescript
const CONFIG_SCHEMA: Record<string, FieldDef[]> = {
  // ... existing entries ...

  size_filter: [
    { type: "number", key: "min_bytes", label: "Min packet size (bytes)", min: 0, max: 65535 },
    { type: "number", key: "max_bytes", label: "Max packet size (0 = unlimited)", min: 0, max: 65535 },
    ...OUTPUT_MODE_FIELDS,   // always spread this at the end for multi-output nodes
  ],
};
```

> **Always spread `OUTPUT_MODE_FIELDS` at the end** of any module that has `maxOutputs > 1`. This adds the "Output Mode" and "LB Mode" dropdowns automatically.

---

## 4. Quick checklist

```
Engine (C):
  [ ] engine/modules/your_module.c   — created with parse_config + process + ops struct
  [ ] engine/graph.h                 — MOD_YOUR_MODULE added to enum
  [ ] engine/graph.c                 — added to module_registry[] and module_type_from_string()
  [ ] engine/modules/module_base.h   — extern your_module_ops declared
  [ ] engine/Makefile                — modules/your_module.c added to SRCS
  [ ] ./build.sh                     — rebuilds successfully

Backend (Python):
  [ ] backend/models/graph_schema.py — "your_module" added to ModuleType and HEAVY/LIGHTWEIGHT_TYPES

Frontend (TypeScript):
  [ ] frontend/src/types/graph.ts          — added to ModuleType union and MODULE_DEFS array
  [ ] frontend/src/components/GraphEditor/NodePalette.tsx   — added to a CATEGORIES entry
  [ ] frontend/src/components/GraphEditor/NodeConfigPanel.tsx — entry in CONFIG_SCHEMA
```

---

## 5. Tips

- **No IPC changes needed.** Stats (pps, bytes, drops) are collected generically for every node — your module gets them for free.
- **`output_mode` is free.** Because your module calls `node_out()`, duplicate and load_balance work automatically without any extra code.
- **Sink nodes** (no output ring) should free packets after processing. See `nic_tx.c` for the pattern.
- **Avoid printf in `process()`** — it's in the hot loop. Use it in `init()` and `destroy()` only.
- **The `template.c` module** is the minimal pass-through skeleton — copy it as your starting point.

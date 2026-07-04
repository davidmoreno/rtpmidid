# Architecture Cleanup Opportunities

> **Status**: Phase 1 ✅ | Phase 2 ✅ | Phase 3 ✅ | Phase 4 ✅

All cleanup phases complete.

This report catalogs code smells, duplication, dead code, and API redundancy
found during a full-codebase architecture audit. Findings are grouped by
severity and category with concrete refactoring suggestions.

For each finding:
- **Detection method**: how the issue was identified
- **Files affected**: key source locations
- **Suggested action**: recommended cleanup
- **Impact**: low / medium / high

---

## 1. RPC API Duplication — Merge or De-duplicate

### 1.1 `connections.add` vs `connections.save` **[DONE]**

**Detection**: line-by-line comparison of `control_rpc.cpp` handlers.

**Files**: `src/control_rpc.cpp` (lines ~460–500 for both handlers),
`src/dm_json_rpc.hpp`.

**What**: Two RPC methods that do nearly the same thing. `connections.add` is
the legacy version (direction=both, enabled=true hardcoded). `connections.save`
is the modern replacement with explicit `direction` and `enabled` params. The
legacy handler is a strict subset of the modern one.

Both call `resolve_side_to_connection_side` and `save_stored_connection`, and
both capture `rows` (unused in `connections.add`).

**Suggested action**:
- Remove `connections.add` from the RPC dispatch (keep for one release with a
  deprecation log warning, then drop).
- Remove `connections_mutate_params_t` and use `connections_save_params_t` with
  defaults (`direction="both"`, `enabled=1`) to serve any remaining callers.

**Impact**: Low effort, cleans docs + two string comparisons in the hot dispatch
chain.

---

### 1.2 `connections.enable` / `connections.disable` as separate methods **[DONE]**

**Detection**: Both share a single `if` branch in `control_rpc.cpp`.

**Files**: `src/control_rpc.cpp` (lines ~520–550), `src/dm_json_rpc.hpp`.

**What**: `connections.enable` and `connections.disable` are separate RPC
methods that differ only in a boolean (`env.method == "connections.enable"`).
They parse the same params (`connections_enable_params_t`), call the same
`set_stored_enabled`, and share the same error handling.

**Suggested action**:
- Merge into `connections.set_enabled` with a `{side_a, side_b, enabled: bool}` param.
- Keep `connections.enable`/`disable` as thin client-side wrappers or deprecate.

**Impact**: Medium — cleaner API, fewer entries in `build_help_entries()`, one
less method comparison in the dispatch chain.

---

### 1.3 `connection_db_t` — legacy vs. new API duplication **[DONE]**

**Detection**: Side-by-side comparison of `connection_db.hpp` public methods.

**Files**: `src/connection_db.hpp`, `src/connection_db.cpp`.

**What**: Three layers of legacy wrappers:

| Legacy method | Modern equivalent | Notes |
|---|---|---|
| `record_connection(a, b)` | `save_connection(row)` | Legacy always `direction=both`, `enabled=true` |
| `get_connections()` → `vector<connection_pair_t>` | `list_connections()` → `vector<stored_connection_t>` | Legacy drops direction/enabled |
| `record_stable_pair(a, b)` (manager) | `save_stored_connection(row)` (manager) | Same pattern at manager level |

**Callers**: `get_connections()` has **zero external callers** (dead code).
`record_connection()` is only called by `record_stable_pair()`, which also has
**zero external callers**. The `connection_pair_t` struct exists only for
`get_connections()`.

**Suggested action**:
- Remove `record_connection()`, `get_connections()`, `connection_pair_t`,
  `record_stable_pair()` — they are completely dead code.
- Keep `save_connection()` / `save_stored_connection()` as canonical API.

**Impact**: Medium — removes 4 methods, 1 struct, simplifies the API surface.

---

### 1.4 `collect_online_devices` duplicated between `connection_db_manager_t` and `peer_spawn` **[DONE]**

**Detection**: Grep for identical pattern (scan `router->status_rows()`,
build `online_device_t` from `compute_device_identity`).

**Files**: `src/connection_db.cpp` (private method, line 307),
`src/peer_spawn.cpp` (free function, line 29).

**What**: Both functions iterate `router->status_rows()`, call
`compute_device_identity(row)`, and build a `vector<online_device_t>`. The
implementations are nearly identical.

**Suggested action**:
- Consolidate to a single free function in `connection_restore.hpp` / `.cpp`
  (already depends on `online_device_t`).
- Expose `collect_online_devices_from_router()` from `peer_spawn.cpp` and
  have `connection_db_manager_t` delegate to it, or move it to a shared utility
  file.

**Impact**: Low — small deduplication, reduces maintenance surface.

---

### 1.5 `format_as(midipeer_event_e)` — redundant with `ENUM_FORMATTER` macros **[DONE]**

**Detection**: The `midipeer_event_e` enum has both `ENUM_FORMATTER_BEGIN`/`END`
macros AND a standalone `format_as()` function.

**Files**: `src/midipeer.cpp` (line 285), `src/midipeer.hpp` (line 180).

**What**: The `ENUM_FORMATTER` macros already produce a `FMT::formatter`
specialization that renders the enum to strings. The standalone `format_as`
function is never called — the formatter dispatches through the macro-generated
switch, not through `format_as`.

The same pattern exists for `format_as(snd_seq_event_type)` in
`src/aseq.cpp:1138`, but there the `BASIC_FORMATTER` macro explicitly calls
`format_as(v)`, making it necessary. Not the case for `midipeer_event_e`.

**Suggested action**: Remove `format_as(midipeer_event_e)` from both
`midipeer.cpp` and `midipeer.hpp`. Not referenced by any call site or formatter.

**Impact**: Low — removes dead code only.

---

### 1.6 `device_registry_t` — `display_name_for` vs `display_name_from_identity` **[DONE]**

**Detection**: Comparison of name resolution logic.

**Files**: `src/device_registry.cpp` (private `display_name_for`, line 27),
`src/peer_factory.cpp` (public `display_name_from_identity`, line 56).

**What**: Two functions that derive a human-readable name from a
`device_identity_t`. `display_name_for` adds a `row_name` override parameter
but the field-priority logic (`name`, `client`, `service` vs `service`, `name`,
`device`, `client`) differs slightly between them.

**Suggested action**:
- Merge into a single function in `device_identity.hpp` / `.cpp`.
- `display_name_for` can become a thin wrapper that calls
  `display_name_from_identity` with the override.

**Impact**: Low — avoids drift between the two name-resolution paths.

---

## 2. Code Duplication — Identical Logic in Multiple Places

### 2.1 `append_unique` — three identical copies **[DONE]**

**Detection**: Grep for `append_unique` across all sources.

**Files**:
- `src/connection_restore.cpp` (line 14, file-local)
- `src/connection_alsa_direct.cpp` (line 33, file-local)

**What**: Both files define identical `static void append_unique(vector<size_t>&, size_t)`.

**Suggested action**:
- Extract to a shared utility header (e.g. `src/utils.hpp` or a new
  `src/internal/algorithm.hpp`).
- Template it as `template<typename T> void append_unique(vector<T>&, T)` for
  general reuse.

**Impact**: Low — trivial deduplication, avoids future copy-paste.

---

### 2.2 Peer identity lookup by `peer_id` — same pattern in two actors **[DONE]**

**Detection**: Both `connection_db_manager_t::device_identity_for_peer()` and
`device_registry_t::status_row_for_peer()` scan `router->status_rows()` by
peer_id.

**Files**: `src/connection_db.cpp` (~line 319),
`src/device_registry.cpp` (private `status_row_for_peer`, line 34).

**What**: Identical iteration: `for (auto &row : router->status_rows()) { if (*row.id == peer_id) return ... }`.

**Suggested action**:
- Add a `router->status_row_for(peer_id_t)` query method and have both consumers
  use it. The router already has `dispatch_query` infrastructure for this.

**Impact**: Medium — avoids O(n) scans in two actors, makes the router the
canonical source.

---

### 2.3 `router_rows_snapshot` thin wrapper in `control_rpc.cpp`

**Detection**: The static helper at line 236 is just `ctx.router->status_rows()`
with a null check.

**Files**: `src/control_rpc.cpp`.

**What**: `router_rows_snapshot(ctx)` returns `ctx.router->status_rows()` but
falls back to empty vector on null. It's used by `monitor.start`, `connections.*`,
and `endpoint.connect`. A plain `ctx.router->status_rows()` with a null check
in the callers would be equally clear and avoid an indirection.

**Suggested action**: Inline or simplify.

**Impact**: Very low — cosmetic.

---

## 3. Dead Code — Remove Unused Functions, Structs, and Methods

### 3.1 Unused RPC param structs in `dm_json_rpc.hpp` **[DONE]**

**Detection**: Grep for usage of each struct beyond `dm_json_rpc.hpp` and
auto-generated `dm_json_generated.hpp`.

**Files**: `src/dm_json_rpc.hpp`.

| Struct | Used? |
|---|---|
| `export_rawmidi_params_t` | **No** — never referenced outside dm_json_rpc.hpp. Appears generated only. |
| `export_rawmidi_error_t` | **No** — same. |
| `create_local_rawmidi_params_t` | **No** — same. |
| `create_network_rtpmidi_client_params_t` | **No** — same. |
| `create_network_rtpmidi_listener_params_t` | **No** — same. |
| `create_local_alsa_peer_params_t` | **No** — same. |
| `listener_help_entry_t` | **No** — same. |

These are remnants of older per-type `router.create.{type}` methods that were
consolidated into the generic `router.create {identity}`. They still get
serialization code generated via dm-json, adding to compile time and binary
size for no benefit.

**Suggested action**: Remove all 7 structs and regenerate `dm_json_generated.hpp`.

**Impact**: Medium — reduces binary size, compile time, and confusion about
whether these APIs exist.

---

### 3.2 `peer_kind_rpc_create_key()` — declared, defined, never called **[DONE]**

**Detection**: Grep for callers of `peer_kind_rpc_create_key` — zero.

**Files**: `src/peer_kind.cpp` (line 61), `src/peer_kind.hpp` (line 37).

**What**: This function maps `peer_kind_e` to old `router.create.{type}` keys
like `"local_rawmidi_t"`, `"peer_device_alsa_seq_t"`. It was part of the
deprecated per-type creation API. No caller exists.

**Suggested action**: Remove.

**Impact**: Low.

---

### 3.3 `peer_kind_is_device`, `peer_kind_is_export`, `peer_kind_is_import` — never called **[DONE]**

**Detection**: Grep for callers — zero beyond their own definitions.

**Files**: `src/peer_kind.cpp` (lines 120–136), `src/peer_kind.hpp` (lines 43–45).

**What**: Three predicate functions that no code calls. They were likely planned
for UI filtering or validation but never wired up.

**Suggested action**: Remove, or if they have a near-future use case, wire them
into the controls that would benefit (e.g. `devices.list` filtering,
`midi.list*` categorization).

**Impact**: Low.

---

### 3.4 `connection_db_t::get_connections()` + `connection_pair_t` — dead code **[DONE]**

**Detection**: Grep for `get_connections` callers — only called internally by
`record_stable_pair`, which is also dead.

**Files**: `src/connection_db.hpp` (lines 41–44, 63),
`src/connection_db.cpp` (lines 262–268).

**What**: Legacy wrapper that discards `direction`/`enabled` metadata. No
external callers.

**Suggested action**: Remove `get_connections()`, `connection_pair_t`.

**Impact**: Low.

---

### 3.5 `connection_db_manager_t::record_stable_pair()` — dead code **[DONE]**

**Detection**: Grep for `record_stable_pair` callers — zero.

**Files**: `src/connection_db.hpp` (line 91),
`src/connection_db.cpp` (line 519).

**What**: Legacy public method on the manager that wraps `record_connection()`.
No caller.

**Suggested action**: Remove.

**Impact**: Low.

---

### 3.6 `format_as(midipeer_event_e)` — dead code **[DONE]**

**Detection**: Grep for callers — zero. The `ENUM_FORMATTER` macros handle
formatting without calling `format_as`.

**Files**: `src/midipeer.cpp` (line 285), `src/midipeer.hpp` (line 180).

**Suggested action**: Remove.

**Impact**: Low.

---

### 3.7 Unused `rows` capture in `connections.add`, `remove`, `enable`/`disable` handlers **[DONE]**

**Detection**: Visual inspection of `control_rpc.cpp` handler for
`connections.add`.

**Files**: `src/control_rpc.cpp` (~line 482).

**What**: The `connections.add` handler captures `const auto rows =
router_rows_snapshot(ctx)` but never uses `rows`. Only `connections.save` and
`endpoint.connect` use this snapshot.

**Suggested action**: Remove the unused capture.

**Impact**: Very low — one unnecessary `status_rows()` call removed.

---

## 4. API Clarity & Genericity

### 4.1 Prefer generic `connections.save` with parameters over separate mutation calls

**Current state**: Four RPC methods for connection mutation:
- `connections.add` — always both, always enabled
- `connections.save` — direction + enabled
- `connections.enable` / `connections.disable` — toggle only

**Suggested consolidation**:
```
connections.save    {side_a, side_b, direction?, enabled?}   # upsert
connections.remove  {side_a, side_b}                          # delete
```

The current `connections.enable`/`disable` are just `connections.save` with
`enabled: true/false` and the existing direction preserved. A future client
capable of saving direction can use `save`; clients that only toggle can call
`save` with just the `enabled` field (the server would preserve direction from
the existing row).

This eliminates 3 methods (add, enable, disable) and 2 param structs.

**Impact**: High — matches the user's preference for generic calls with
parameters over multiple similar calls.

---

### 4.2 `router.create` / `connect` / `endpoint.connect` — three connect paths **[DONE]**

**Resolution**: The `connect` convenience handler now serialises the identity
via `id.serialize()` and calls `create_peer_from_string()` — the same
function used by `router.create {identity}`. Both paths share the same
peer-factory resolution logic.

**Current state**: Three RPC methods that create or connect peers:
- `connect {hostname, port?, name?}` — convenience for remote RTP-MIDI
- `router.create {identity}` — create a peer from identity string
- `endpoint.connect {from, to, bidi?}` — creates peers if needed + connects

`connect` is just `router.create` with `type_prefix="alsa_listener"` and
fields mapped from hostname/port/name. The `connect` handler manually
constructs a `device_identity_t` and calls `create_peer`, which is exactly
what `router.create` does for strings.

**Suggested action**:
- Move the `connect {hostname, port?, name?}` logic into the client side
  (Web UI / CLI) so it just calls `router.create` with
  `identity: "alsa_listener:service=...,hostname=...,port=..."`.
- Or accept that `connect` is a convenience and document it as such, but
  remove the manual identity construction from the server — parse the params
  into an identity string and delegate to `router.create`.

**Impact**: Medium — one fewer special case in the dispatch chain, clearer
that `router.create` handles all peer creation.

---

### 4.3 `connections.list` returns `enabled` as a field of the result

**Detection**: `connections_list_result_t` has `int32_t enabled = 0` set to 1
when DB is available. This is metadata about the transport, not the connections
themselves.

**Files**: `src/dm_json_rpc.hpp`, `src/dm_json_status.hpp`.

**What**: The `enabled` field indicates whether the connection database is
available (used by the Web UI to show/hide the connections tab). It's embedded
into the result object rather than being a separate concern.

The same pattern exists in `devices_list_result_t`.

**Suggested action**: Consider a top-level `features` or `capabilities` field in
the `status` response instead of per-endpoint `enabled` flags. The Web UI could
query `status` once to know what's available.

**Impact**: Low — minor API design refinement. Not urgent.

---

### 4.4 `resolve_side_to_connection_side` captures `rows` in 3 of 4 callers but never uses it in 1

**Detection**: The `connections.add` handler captures `rows` but never uses it.

**Files**: `src/control_rpc.cpp`.

**What**: Four handlers (`connections.save`, `connections.add`, `connections.remove`,
`connections.enable`/`disable`) all have this pattern:

```cpp
const auto rows = router_rows_snapshot(ctx);
const auto sa = resolve_side_to_connection_side(p.side_a);
const auto sb = resolve_side_to_connection_side(p.side_b);
```

Only `connections.save` actually needs `rows` (passed to a callback). The
other four capture it unnecessarily. After removing `connections.add`, the
number drops to 3.

**Suggested action**: After cleanup, only capture `rows` where needed.

**Impact**: Very low.

---

## 5. Structural / Design Observations

### 5.1 Signal dispatch is duplicated inline in `post_signal_*` methods

**Files**: `src/midirouter.cpp` (lines 71–117).

**What**: Each `post_signal_peer_added`, `post_signal_peer_removed`,
`post_signal_connected`, `post_signal_disconnected`, `post_signal_peer_event`
follows the same pattern:

```cpp
void midirouter_t::post_signal_X(args...) {
  if (sync_mode()) {
    X_event(args...);
    return;
  }
  enqueue(router_command_t{router_cmd::signal_X_t{args...}}, NORMAL);
}
```

Five near-identical methods. Could be a template or macro.

**Suggested action**: Low priority — the duplication is mechanical and easy to
verify. A macro `DEFINE_POST_SIGNAL(name, signal, ...)` would reduce 40 lines to
5. But macros hurt debuggability. Keep as-is unless the number grows.

**Impact**: Very low — purely cosmetic at current scale.

---

### 5.2 `midirouter_t::enqueue_*` backwards-compat aliases **[DONE]**

**Files**: `src/midirouter.cpp` (lines 1011–1040),
`src/midirouter.hpp` (lines 122–129).

**What**: Six `enqueue_*` methods that are thin wrappers around the public
`send_midi`, `connect`, `disconnect`, `remove_peer`, `event` methods. They
always succeed (return true) and were kept for backwards compatibility.

Callers still exist in non-router code:
- `peer_device_rtpmidi_session.cpp`
- `peer_export_alsa_network.cpp`
- `rtpmidiremotehandler.cpp`
- `webui_midi_monitor_peer.cpp`
- `peer_import_alsa_rtp.cpp`

**Suggested action**: Migrate the remaining callers to use `send_midi()`,
`connect()`, `disconnect()`, `remove_peer()`, and `event()` directly. Then
remove the `enqueue_*` aliases.

**Impact**: Low — cleaner router API without two ways to do the same thing.

---

### 5.3 `control_rpc_dispatch_line` — giant if-else chain **[DONE]**

**Resolution**: Replaced the linear O(n) if-else chain with an O(1)
`std::unordered_map<std::string, rpc_handler_fn>` dispatch table. Each
handler is a named free function, making them individually readable
and testable. Peer commands (`<id>.<subcmd>`) are handled before the
map lookup. Backwards-compatible aliases (`connections.enable`,
`connections.disable`) map to the same handler as `connections.set_enabled`.

**Files**: `src/control_rpc.cpp` (single 500+ line function).

**What**: All ~30 RPC methods are dispatched in a linear if-else chain. This
works correctly but:

1. Every method string is compared against ~30 candidates per call.
2. Adding a method requires touching the giant function.
3. Harder to unit-test individual handlers.

**Suggested action**: Replace with a `std::unordered_map<string, handler_fn>`
dispatch table. Each handler would be a standalone function/lambda. The peer
command regex would be tried first, then the map lookup. This also enables
`help` to be auto-generated from the table.

**Impact**: Medium — better extensibility, marginal perf improvement, easier
to test.

---

## 6. Summary — Priority Matrix

| # | Item | Effort | Impact | Category |
|---|------|--------|--------|----------|
| 1.1 | Remove `connections.add` | Low | High | API de-dup |
| 1.2 | Merge `enable`/`disable` into `set_enabled` | Low | Medium | API de-dup |
| 1.3 | Remove `record_connection`/`get_connections`/`record_stable_pair` | Low | Medium | Dead code |
| 3.1 | Remove 7 unused RPC param structs | Low | Medium | Dead code |
| 3.2–3.6 | Remove `peer_kind_rpc_create_key`, `peer_kind_is_*`, dead `format_as`, `connection_pair_t` | Low | Low | Dead code |
| 2.1 | Deduplicate `append_unique` | Low | Low | Duplication |
| 2.2 | Add `router->status_row_for(peer_id)` | Medium | Medium | Duplication |
| 4.1 | Consolidate connection mutation into `save`/`remove` | Medium | High | API clarity |
| 4.2 | Route `connect` through `router.create` | Low | Medium | API clarity |
| 5.3 | Dispatch table for RPC methods | Medium | Medium | Architecture |
| 5.2 | Remove `enqueue_*` aliases | Low | Low | API cleanup |
| 1.4 | Deduplicate `collect_online_devices` | Low | Low | Duplication |
| 1.5 | Remove `format_as(midipeer_event_e)` | Trivial | Low | Dead code |

## 7. Suggested Implementation Order

**Phase 1 — Quick wins (dead code removal, ~1 PR)**:
- Items 3.1–3.7: Remove unused structs, functions, methods
- Item 1.5: Remove `format_as(midipeer_event_e)`

**Phase 2 — API consolidation (~2 PRs)**:
- Item 1.3: Remove legacy `connection_db_t` methods + `connection_pair_t`
- Item 1.1: Deprecate/remove `connections.add`
- Item 1.2: Merge `connections.enable`/`disable` into `connections.set_enabled`
- Item 5.2: Migrate `enqueue_*` callers, remove aliases

**Phase 3 — Deduplication & structure (~2 PRs)**:
- Item 2.1: Extract `append_unique`
- Item 1.4: Consolidate `collect_online_devices`
- Item 2.2: Add `status_row_for(peer_id_t)` to router
- Item 1.6: Merge `display_name_for` / `display_name_from_identity`

**Phase 4 — Architecture improvement (~1 PR)**:
- Item 4.2: Route `connect` through `router.create`
- Item 5.3: Dispatch table for RPC methods

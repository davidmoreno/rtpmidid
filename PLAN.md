# PLAN: Queryable Ring-Buffer Log with Web UI

## Overview

Add an in-memory ring buffer of the last N log messages (default 1024) with
structured tags, a JSON-RPC query endpoint with filtering, and a new **Logs**
tab in the Web UI with clickable tag chips.

Log messages are emitted in **logfmt** format so each tag is a searchable
`key=value` pair.

---

## 1. Data model

### 1.1 `log_entry_t` (C++)

Keep the struct simple — tags live **inside the message** as logfmt `key=value`
pairs. No separate tag fields.

```cpp
// include/rtpmidid/log_entry.hpp
struct log_entry_t {
  uint64_t    seq;           // monotonic sequence number
  uint64_t    timestamp_us;  // steady_clock::now() at emission time
  int         level;         // 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR
  std::string file;          // basename only, e.g. "midirouter.cpp"
  int         line;
  std::string message;       // logfmt body with inline tags (see §1.2)
};
```

Tags are part of the message string. They are extracted at query time by
a small logfmt parser. No separate tag fields on the struct.

### 1.2 logfmt format for `message`

Every `message` field follows logfmt conventions:

```
level=INFO file=midirouter.cpp:164 peer_id=5 component=router msg="Added peer type=rtpmidi_client peer_id=5"
```

Rules:
- Keys are lowercase `[a-z_][a-z0-9_]*`
- Values without spaces/equals/special chars are bare: `peer_id=5`
- Values with spaces or special chars are quoted: `msg="some text"`
- Quotes inside values are backslash-escaped: `msg="quote \"here\""`

This makes every tag grep-able with standard tools and directly usable as
filter keys in the query API.

---

## 2. Structured logging macros (C++)

Replace the current printf-style macros with variants that accept tags.

### 2.1 How to add tags — no new macros needed

Use the existing `INFO`, `DEBUG`, `WARNING`, `ERROR` macros. Put logfmt
`key=value` pairs at the start of the format string, before the human
message. Use the format arguments to inject runtime values.

```cpp
// Before (untagged):
INFO("Added peer type={} peer_id={}", peer->get_type(), pid);

// After (tagged — same macro, logfmt prefix):
INFO("peer_id={} component=router Added peer type={} peer_id={}",
     pid, peer->get_type(), pid);
```

Resulting message:
```
peer_id=5 component=router Added peer type=rtpmidi_client peer_id=5
```

The `key=` parts in the format string are literal text. The `{}` after
them are the values — same `fmt` formatting you already use. The logfmt
parser doesn't care whether `peer_id=5` came from a macro or a format
string.

**Pattern catalog:**

```cpp
// Peer lifecycle (midirouter.cpp)
INFO("peer_id={} component=router Added peer type={}", pid, type);
DEBUG("peer_id={} component=router Connect {} -> {}", from, from, to);
WARNING("peer_id={} component=router Sending to unknown peer {} -> {}", from, from, to);
ERROR("peer_id={} component=router Exception in peer thread: {}", pid, e.what());

// RTP session (rtppeer.cpp, lib/rtppeer.cpp)
INFO("peer_id={} component=rtp session_id={:x} Got confirmation from {}",
     peer_id, remote_ssrc, remote_name);
INFO("peer_id={} component=rtp session_id={:x} Latency: {:.2f} ms",
     peer_id, remote_ssrc, latency);
WARNING("peer_id={} component=rtp session_id={:x} Invitation rejected (NO)",
     peer_id, remote_ssrc);

// Control / connect (control_rpc.cpp)
INFO("component=control endpoint.connect {} -> {} bidi={}", from, to, bidi);
INFO("peer_id={} component=control monitor.start uuid={}", peer_id, uuid);

// mDNS (mdns_rtpmidi.cpp)
INFO("component=mdns Discovered {} at {}", name, address);
INFO("component=mdns Removed {} from {}", name, address);

// Connection persistence (connection_db.cpp)
INFO("component=database connection_id=\"{}\" Saved connection {} <-> {}",
     conn_id, side_a, side_b);
```

**What makes a good tag:**
- `peer_id=N` — mandatory for any log inside a peer context (router, rtp, peer thread)
- `component=name` — router, rtp, control, mdns, database, monitor, alsa
- `session_id=hex` — RTP SSRC or monitor UUID
- `connection_id=string` — identity pair for persisted connections

---

## 3. Ring buffer (`log_buffer_t`)

### 3.1 Core structure

```cpp
// include/rtpmidid/log_buffer.hpp
class log_buffer_t {
  std::vector<log_entry_t> ring_;   // pre-allocated to capacity
  std::atomic<uint64_t>    write_pos_{0};
  std::atomic<uint64_t>    seq_{0};
  size_t                   capacity_;
  mutable std::shared_mutex mutex_;  // read-write lock

public:
  explicit log_buffer_t(size_t capacity = 1024);

  void push(log_entry_t entry);  // lock-free write, wraps around

  // Query (read-locked)
  std::vector<log_entry_t> query(const log_query_t &q) const;
};
```

### 3.2 Thread safety

- **Writes**: lock-free via atomic `write_pos_` and `seq_`. Multiple producers
  (all threads call log macros) write to different slots. The ring is
  pre-allocated — no allocations on the hot path.
- **Reads**: `std::shared_mutex` allows concurrent queries. Read lock briefly.

### 3.3 Singleton

A global `log_buffer_t g_log_buffer` lives in `lib/logger.cpp`. The logger
thread pushes to it after formatting. This avoids contention — the logger
thread is the sole consumer from `log_queue` and sole pusher to the ring
buffer.

Tags are NOT pre-extracted on push — they stay in the message string.
Extraction happens at query time via the logfmt parser.

### 3.4 logfmt parser

A small utility function (in `lib/log_buffer.cpp`) that:
1. Scans the message for `key=value` and `key="quoted value"` pairs at the
   start of the string.
2. Stops at the first word that isn't a `key=value` pair — the rest is the
   free-text body (accessible as the `msg` tag if desired).
3. Returns a `std::map<std::string, std::string>`.

```cpp
// include/rtpmidid/log_buffer.hpp
std::map<std::string, std::string> parse_logfmt_tags(std::string_view message);
```

This parser is called during `log_buffer_t::query()` to match filter
criteria and to populate the `tags` field in `log_query_entry_t`.

---

## 4. Query API

### 4.1 RPC method: `log.query`

**Request:**

```json
{
  "method": "log.query",
  "params": {
    "q": "{peer_id=\"5\", component=\"router\", level=\"error\"}",
    "since_seq": 0,
    "since_us": 0,
    "limit": 100
  }
}
```

All fields optional. `q` defaults to `""` (return all). `limit` defaults to 100, max 1024.

### 4.2 `q` query language (LogQL-style)

Modeled after [Grafana Loki LogQL](https://grafana.com/docs/loki/latest/query/log_queries/).
The backend parses `q` into filters.

**Syntax:**

```
q = stream_selector (pipe_expr)*

stream_selector = "{" key "=" value ("," key "=" value)* "}"
                | ""                           # empty = no stream filter

pipe_expr = "|=" value         # line contains (case-insensitive substring)
          | "!=" value         # line does NOT contain
          | "|~" regex         # line matches regex (future)
          | "!~" regex         # line does NOT match regex (future)

key   = [a-z_][a-z0-9_]*
value = bare       | "\"" .* "\""
bare  = [^ \t\n,{}|"=]+    # unquoted: no spaces or special chars
```

**Semantics:**

- `{...}` — **stream selector**: matches logfmt tags in the message.
  Multiple comma-separated pairs are AND-ed. This is the only filter
  that matches structured tags.
- `|= "text"` — **line filter**: full-text search over the entire raw
  message string (case-insensitive substring).
- `!= "text"` — **negated line filter**: entry must NOT contain text.
- If `{...}` is omitted, all entries pass the stream selector.
- Multiple `|=` / `!=` pipeline stages chain: all must match.

**Examples:**

```
# Stream selector: all entries with these tags
{peer_id="5"}
{peer_id="5", component="router"}
{level="error"}

# Stream + line filter
{peer_id="5"} |= "timeout"

# Multiple line filters (AND)
{component="rtp"} |= "Got confirmation" |= "synth.local"

# Negation: everything EXCEPT debug
{} != "level=debug"

# Without stream selector (filter all entries)
|= "connection refused"

# OR is implicit via comma in stream selector values? No —
# for OR across streams, run multiple queries or use regex:
{peer_id="5"} |~ "(timeout|refused)"

# Empty q = return all
""
```

**Design note:** LogQL-style separates structured filtering (`{...}`)
from full-text filtering (`|=`, `!=`). This avoids the ambiguity of
"is `peer_id=5` a tag match or a substring match?" — tags go in `{}`,
free text goes in `|=`. The parser is ~80 lines (simpler than the
AND/OR/paren grammar).

### 4.3 Request params (full)

```json
{
  "method": "log.query",
  "params": {
    "q": "{peer_id=\"5\", component=\"router\", level=\"error\"}",
    "since_seq": 0,
    "since_us": 0,
    "limit": 100
  }
}
```

All fields optional. `q` defaults to `""` (return all). `limit` defaults to 100, max 1024.

### 4.4 Response

```json
{
  "result": {
    "entries": [
      {
        "seq": 4823,
        "timestamp_us": 1718400000123456,
        "level": 1,
        "file": "midirouter.cpp",
        "line": 164,
        "tags": {"peer_id": "5", "component": "router"},
        "message": "peer_id=5 component=router Added peer type=rtpmidi_client peer_id=5"
      }
    ],
    "total_matches": 42,
    "buffer_capacity": 1024,
    "oldest_seq": 3800,
    "newest_seq": 4842
  }
}
```

Tags are extracted from the logfmt `message` string at query time by the
logfmt parser. The `tags` field is a convenience map so the Web UI doesn't
need its own parser — the raw `message` field preserves the full logfmt line
for display and copy-paste.

### 4.6 Backend query parsing

The backend parses `q` into a simple query struct:

```cpp
// include/rtpmidid/log_buffer.hpp

struct logql_query_t {
  // Stream selector: key → value (AND-ed), empty = match all
  std::map<std::string, std::string> tags;
  // Pipeline stages: {{op, value}, ...} applied in order
  struct stage_t {
    enum op_t { CONTAINS, NOT_CONTAINS } op;
    std::string value;
  };
  std::vector<stage_t> pipeline;
};

logql_query_t parse_logql(std::string_view q);
```

**Parser:** ~80 lines.
1. If `q` starts with `{`, parse comma-separated `key="value"` pairs
   into `tags`. Stop at `}`.
2. Parse remaining pipeline: `|= "value"` → CONTAINS, `!= "value"`
   → NOT_CONTAINS. Multiple stages are chained.
3. If `q` is empty or `""`, return match-all query.

**Evaluator:** walks the ring buffer:
1. For each entry, extract tags from `message` via `parse_logfmt_tags()`
2. Stream filter: all `logql_query_t::tags` must match the entry's tags
3. Pipeline: each stage applied in order to the raw `message` string
   (case-insensitive substring for CONTAINS, negated for NOT_CONTAINS)
4. First entry failing any stage is skipped

Complexity: O(n) per query, where n = entries scanned. The ring buffer
scan is bounded by `limit` and `since_seq`.

### 4.5 DM-JSON structs

```cpp
// src/dm_json_rpc.hpp — new structs

struct log_query_params_t {
  std::optional<std::string> q;         // logfmt query string, parsed by backend
  std::optional<uint64_t>    since_seq;
  std::optional<uint64_t>    since_us;
  std::optional<int>         limit;     // default 100, max 1024
};

// Lightweight entry for query results — tags extracted from logfmt at query time
struct log_query_entry_t {
  uint64_t                               seq;
  uint64_t                               timestamp_us;
  int                                    level;
  std::string                            file;
  int                                    line;
  std::string                            message;       // full logfmt line
  std::map<std::string, std::string>     tags;          // extracted key=value pairs
};

struct log_query_result_t {
  std::vector<log_query_entry_t> entries;
  int                            total_matches;
  int                            buffer_capacity;
  uint64_t                       oldest_seq;
  uint64_t                       newest_seq;
};
```

### 4.4 Registration in `control_rpc.cpp`

Add `log.query` to `build_help_entries()` and a handler block in
`control_rpc_dispatch_line()`:

```cpp
if (env.method == "log.query") {
  auto p = parse_rpc_params<log_query_params_t>(params);
  auto result = g_log_buffer.query(p);
  return respond(env, result);
}
```

---

## 5. Configuration

### 5.1 INI settings

Add a `[log]` section to `default.ini`:

```ini
[log]
# Ring buffer capacity for queryable logs (RPC + Web UI).
# Must be between 256 and 65536 (power of 2 recommended).
buffer_capacity=1024
```

### 5.2 Settings struct

Add to `settings.hpp`:

```cpp
struct log_settings_t {
  int buffer_capacity = 1024;
};
```

Parse in `ini.cpp` alongside other sections.

---

## 6. Event subscription (optional future)

Once the buffer exists, add a `log.new_entry` event channel. The Web UI can
subscribe and append new entries in real time (tail -f style).

**Defer to phase 2** — the polling-based query is sufficient for v1.

---

## 7. Web UI — Logs tab

### 7.1 New component: `LogsTab.tsx`

File: `frontend/src/tabs/LogsTab.tsx`

Features:

| Feature                      | Behavior                                                                                                                                       |
| ---------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| **Live scrollable log feed** | Auto-scrolls to bottom on new entries. "Scroll to bottom" button appears when scrolled up.                                                     |
| **Tag chips**                | Each tag value rendered as a clickable pill. Click to add to filter. Re-click to remove.                                                       |
| **Color-coded tags**         | `peer_id=…` in blue, `component=…` in green, `session_id=…` in purple, `level=ERROR` in red background.                                        |
| **Filter bar**               | Shows active filters as removable chips. Each chip represents a `key=value` token in the `q` string. Free-text search box for bare word terms. |
| **OR support**               | Shift-click a tag chip to add as OR (e.g. `peer_id=5 OR peer_id=7`). Visual grouping.                                                          |
| **Level filter**             | Toggle buttons: DEBUG / INFO / WARN / ERROR (multi-select).                                                                                    |
| **Time range**               | Optional "Last 5 min / 15 min / 1 hour / All" dropdown.                                                                                        |
| **Pause/Resume**             | Button to freeze the feed while reading.                                                                                                       |
| **Clear**                    | Button to clear filters and reset.                                                                                                             |
| **Copy entry**               | Click an entry row to copy it to clipboard.                                                                                                    |

### 7.2 Data flow

```
Component mounts
  → rpc.call("log.query", {limit: 100})
  → display entries

Every 2 seconds (while tab active):
  → rpc.call("log.query", {q: activeFilterQ, since_seq: last_seen_seq})
  → prepend new entries to list

User clicks tag chip:
  → build q string: "{peer_id=\"5\", component=\"router\"}"
  → rpc.call("log.query", {q: "{peer_id=\"5\", component=\"router\"}", limit: 100})
  → re-render filtered list

The frontend never parses logfmt — it just builds a `q` string by joining
the active filter tokens. The backend does all the parsing and matching.
```

### 7.3 Visual design

```
┌─────────────────────────────────────────────────────────────┐
│  LOGS                                          [Pause] [Clear] │
├─────────────────────────────────────────────────────────────┤
│  [DEBUG] [INFO] [WARN] [ERROR]  │ peer_id=5 ✕ │ Search…    │
│                                  │ component=router ✕        │
├─────────────────────────────────────────────────────────────┤
│ 22:30:01.234  INFO  midirouter.cpp:164                      │
│   [peer_id=5] [component=router] Added peer type=...        │
│                                                             │
│ 22:30:01.456  DEBUG rtppeer.cpp:150                         │
│   [peer_id=5] [component=rtp] [session_id=a1b2]             │
│   Got confirmation from synth.local, initiator_id: ...      │
│                                                             │
│ 22:30:02.001  WARN  control_rpc.cpp:282                     │
│   endpoint.disconnect: skip unpersist (alsa_seq:... → ...)  │
│                                                             │
│  ──── scroll to bottom ────                                 │
└─────────────────────────────────────────────────────────────┘
```

### 7.4 Tag color scheme

| Tag key         | Color                                                    | CSS class               |
| --------------- | -------------------------------------------------------- | ----------------------- |
| `level`         | red (ERROR), yellow (WARNING), blue (INFO), gray (DEBUG) | `ui-log-level-{level}`  |
| `peer_id`       | indigo/blue                                              | `ui-log-tag-peer`       |
| `component`     | emerald/green                                            | `ui-log-tag-component`  |
| `connection_id` | amber/orange                                             | `ui-log-tag-connection` |
| `session_id`    | purple                                                   | `ui-log-tag-session`    |
| `file`          | gray                                                     | `ui-log-tag-file`       |

### 7.5 OR filter UX

LogQL-style doesn't have native OR across different streams. The Web UI
handles this with multi-query tabs or by using `|~` regex in the line
filter (once regex is implemented):

```
{component="rtp"} |~ "(peer_id=5|peer_id=7)"
```

For v1, the Web UI can run two queries and merge results, or the user
can use regex on the raw message.

### 7.6 Registration in `app.tsx`

```tsx
{
  id: "logs",
  label: "Logs",
  content: (
    <LogsTab rpc={rpc} />
  ),
}
```

Add to the tabs array after "Connections".

---

## 8. Implementation phases

### Phase 1 — Backend core (C++) ✅ COMPLETED

| Step | File(s)                                            | Description                                                      |
| ---- | -------------------------------------------------- | ---------------------------------------------------------------- |
| 1a   | `include/rtpmidid/log_buffer.hpp`                  | `log_entry_t` + `log_buffer_t` + `parse_logfmt_tags` + `parse_logql` |
| 1b   | `lib/log_buffer.cpp`                               | Ring buffer (push + query + resize) + logfmt parser + LogQL parser   |
| 1c   | `lib/logger.cpp` + `logger.hpp`                    | Hook ring buffer via `push_to_buffer()` in `log()` template          |
| 1d   | `src/dm_json_rpc.hpp` *(dm-json codegen)*          | `log_query_params_t`, `log_query_entry_t`, `log_query_result_t`  |
| 1e   | `src/control_rpc.cpp`                              | `log.query` handler + help entry + extern g_log_buffer           |
| 1f   | `src/main.cpp` + `settings.hpp` + `ini.cpp` + `default.ini` | `[log] buffer_capacity` config + resize at startup        |
| 1g   | `tests/test_log_buffer.cpp` + `tests/CMakeLists.txt` | 18 test cases: push/wrap/query/filter/logfmt/LogQL              |

### Phase 2 — Structured logging migration

| Step | Files | Description |
|------|-------|-------------|
| 2a   | `src/midirouter.cpp`     | Tag peer add/remove/connect/disconnect with `peer_id=` logfmt prefix |
| 2b   | `lib/rtppeer.cpp`        | Tag RTP lifecycle with `peer_id=`, `component=rtp`, `session_id=` |
| 2c   | `src/control_rpc.cpp`    | Tag endpoint.connect/disconnect, monitor.start/stop |
| 2d   | `src/peer_*.cpp`         | Tag peer creation and errors |
| 2e   | `src/mdns_rtpmidi.cpp`   | Tag mDNS events with `component=mdns` |
| 2f   | `src/connection_db.cpp`  | Tag connection persistence with `component=database`, `connection_id=` |

### Phase 3 — Web UI

| Step | Files                           | Description                                         |
| ---- | ------------------------------- | --------------------------------------------------- |
| 3a   | `frontend/src/tabs/LogsTab.tsx` | Log viewer component with filters, chips, OR, pause |
| 3b   | `frontend/src/app.tsx`          | Register Logs tab                                   |
| 3c   | Styles                          | Tag color CSS classes                               |
| 3d   | Tests                           | `logs.query.test.ts` — filter parsing, OR logic     |

### Phase 4 — Polish (optional)

| Step | Description                                              |
| ---- | -------------------------------------------------------- |
| 4a   | Event subscription `log.new_entry` for real-time push    |
| 4b   | Export filtered logs as text (copy or download)          |
| 4c   | Log entry detail popup on click (full message, all tags) |

---

## 9. Example: debugging a failing connection

Before:
```bash
# grep the log file, hope you catch the right messages
grep "peer.*5" /var/log/rtpmidid.log | tail -50
```

After (in Web UI):
```
1. Navigate to Logs tab
2. Click [ERROR] level filter
3. Type "peer 5" in search
   → instantly see all errors related to peer 5
4. Shift-click peer_id=5 → shows ONLY peer 5 messages
5. Click connection_id chip → narrows to that connection's lifecycle
```

Or via CLI:
```bash
rtpmidid-cli log.query '{"q":"{peer_id=\\"5\\", level=\\"error\\"}","limit":20}' | jq
```

---

## 10. Files created / modified

### New files
```
include/rtpmidid/log_buffer.hpp       # log_entry_t, log_query_t, log_buffer_t
lib/log_buffer.cpp                     # ring buffer implementation
test/log_buffer.cpp                    # unit tests
frontend/src/tabs/LogsTab.tsx          # Web UI log viewer
```

### Modified files
```
include/rtpmidid/logger.hpp            # structured logging macros
lib/logger.cpp                         # hook g_log_buffer.push()
src/dm_json_rpc.hpp                    # log_query_params_t, log_query_result_t
src/control_rpc.cpp                    # log.query handler
src/settings.hpp                       # log_settings_t
src/ini.cpp                            # [log] section parser
default.ini                            # [log] buffer_capacity
src/dm_json_generated.hpp              # auto-generated from dm-json structs
frontend/src/app.tsx                   # register LogsTab
docs/development/control-protocol.md   # document log.query
docs/development/configuration.md      # document [log] section
docs/user/web-ui.md                    # document Logs tab
```

---

## 11. Design decisions & rationale

### Why logfmt instead of structured JSON for message body?

- **Human-readable** in the Web UI without parsing
- **grep-friendly**: `grep "peer_id=5" logfile` works without tooling
- **Extensible**: new tags don't break parsers — unknown keys are ignored
- **Single source of truth**: tags are in the message. No separate columns
  to keep in sync. The log_entry_t struct stays small.
- The logfmt parser is trivial (~40 lines, no dependencies)

### Why ring buffer instead of SQLite?

- **Zero disk I/O** on the hot log path — no fsync or WAL overhead
- **Fixed memory** — predictable, no unbounded growth
- **Thread safety** is simpler: atomic writes, read-write lock for queries
- Persistence is a separate concern — logs already go to stdout/stderr and can
  be piped to journald or a file

### Why poll-based for Web UI instead of event push?

- Simpler v1 implementation
- The Web UI refreshes every 2 seconds — good enough for a log viewer
- `since_seq` parameter makes polling efficient (only new entries)
- Phase 4 can add `log.new_entry` event channel for real-time mode

### Why not change existing macros?

- Backward compatibility: all 200+ existing `INFO(...)` calls keep working
- Add logfmt `key=value` prefixes to format strings — no new macros, same `INFO`/`DEBUG`/`WARNING`/`ERROR`
- Gradual migration — tag the lines that matter most for debugging first

---

## 12. Risk assessment

| Risk                                   | Mitigation                                                                                                                                                                                                         |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Ring buffer write contention           | Single producer (logger thread) eliminates contention. Atomic `write_pos_` ensures correctness.                                                                                                                    |
| logfmt parsing overhead at query time  | Parser is trivial (~40 lines, no allocations for bare values). Query result sets are limited to `limit` entries (default 100). Parsing only runs for entries that pass `since_seq`/`level`/`since_us` pre-filters. |
| `dm-json` codegen for new structs      | Same pattern as existing `dm_json_rpc.hpp` structs. Run `make test-gen` after adding.                                                                                                                              |
| Frontend performance with 1000 entries | Virtualize the list (render only visible rows). `limit` parameter keeps query results small.                                                                                                                       |
| OR filter complexity in URL/state      | Keep as local component state (not URL hash). OR sets are transient visual groupings.                                                                                                                              |
| Tags missing from existing log calls   | Gradual migration: add logfmt `key=` prefixes to existing `INFO(...)` format strings. Untagged messages still appear — they just match fewer filters. No breaking changes. |

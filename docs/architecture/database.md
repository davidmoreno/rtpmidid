# Database and persistence

rtpmidid optionally persists devices and connections in a single SQLite file
configured via `[database] path=…` in the INI. An empty path disables DB
features.

## SQLite wrapper

Shared RAII helpers in [`src/sqlite_db.hpp`](../../src/sqlite_db.hpp):

- `sqlite_db_t` — open, `exec`, `prepare`, `has_table_column`, per-DB mutex
- `sqlite_stmt_t` — bind, `step`, column accessors

Both `connection_db_t` and `device_db_t` use this wrapper (separate connections
to the same file, as before the refactor).

## Schema

### `connections` table (v2)

```sql
CREATE TABLE connections (
  side_a    TEXT NOT NULL,   -- stored query (may contain [ ] fields)
  side_b    TEXT NOT NULL,
  direction TEXT NOT NULL,   -- 'a2b' | 'b2a' | 'both'
  enabled   INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY (side_a, side_b)
);
```

- Legacy DBs without `direction`/`enabled` are migrated in-place by
  `connection_db_t::migrate_schema()`.
- `canonicalize_stored_connection()` sorts sides lexicographically and flips
  direction when sides swap.

Implementation: [`src/connection_db.hpp`](../../src/connection_db.hpp),
[`src/connection_db.cpp`](../../src/connection_db.cpp).

### `devices` table

```sql
CREATE TABLE devices (
  identity   TEXT PRIMARY KEY,  -- full key=value identity
  type       TEXT NOT NULL,
  name       TEXT,              -- display name
  source     TEXT NOT NULL,     -- 'discovered' | 'ini' | 'manual'
  first_seen INTEGER NOT NULL,
  last_seen  INTEGER NOT NULL
);
```

`online` is **not** stored; computed at runtime.

Implementation: [`src/device_db.hpp`](../../src/device_db.hpp),
[`src/device_db.cpp`](../../src/device_db.cpp).

## connection_db_manager_t

Owns auto-reconnect logic. Hooks router and ALSA signals:

| Signal | Action |
|--------|--------|
| `peer_added_event` | Try restore saved connections for new peer |
| `connected_event` / `disconnected_event` | Track pairs for optional recording |
| `peer_event` | Reconnect on `CONNECTED_PEER` |
| `aseq_port_added` | Retry pure-ALSA `aconnect` pairs |

Key methods:

- `apply_saved_connections()` — startup restore
- `check_reconnects_for_all()` — manual/full refresh
- `save_stored_connection()` / `set_stored_enabled()`
- `try_auto_aconnect_all_alsa_pairs()` — pure ALSA restore

### Connection restore planning

[`src/connection_restore.cpp`](../../src/connection_restore.cpp):

1. `collect_online_devices()` — map live peers to `device_identity_t`
2. `match_side_to_devices()` — expand each stored side (query or identity)
3. `plan_connection_restore()` — cartesian product when both sides match
   multiple peers; emit directed `connect_action_t` only for missing edges

Disabled rows (`enabled=0`) are skipped.

### Pure ALSA restore

[`src/connection_alsa_direct.cpp`](../../src/connection_alsa_direct.cpp) matches
ALSA-side stored queries and plans `aconnect`/`adisconnect` actions honoring
stored direction.

## device_registry_t

Actor-style registry merging three sources into one device list:

| Source | How it enters |
|--------|---------------|
| INI | `seed_ini()` at startup |
| Discovered | Router signals when peers appear/disappear |
| Manual | `devices.add_manual` RPC |

Runs on its own thread with typed `device_registry_command_t` queue (no mutex
on `devices_`). Reads use `reply_channel_t` like the router.

Key files:

- [`src/device_registry.hpp`](../../src/device_registry.hpp)
- [`src/device_registry_command.hpp`](../../src/device_registry_command.hpp)
- [`src/device_identity_from_peer.cpp`](../../src/device_identity_from_peer.cpp) —
  maps `router_peer_row_t` → `device_identity_t`

### Stale device cleanup

`select_stale_discovered_devices()` (pure helper) returns offline discovered
devices older than the cutoff **and** not matched by any stored connection
query.

- `sweep_stale_discovered()` — erases from memory + DB, fires `changed_event`
- Scheduled via `cron_tasks_t` (24 h interval, ~1 month max age)
- `set_referenced_queries_provider()` — `main.cpp` injects queries parsed from
  all `connections` sides so referenced devices are never pruned

## Control plane RPC

| Method | DB effect |
|--------|-----------|
| `connections.save` | Upsert row with direction + enabled |
| `connections.add` | Legacy bidirectional save |
| `connections.remove` | Delete row |
| `connections.enable` / `disable` | Toggle `enabled` |
| `connections.list` | List all rows + match status |
| `devices.add_manual` | Upsert manual device |
| `devices.remove` | Delete manual entry |
| `devices.list` | Merged registry view |

`endpoint.connect` may persist ALSA pairs when `[database]` is enabled.

## Wiring in main

[`src/main.cpp`](../../src/main.cpp):

1. Open `connection_db_t` + `device_db_t` on `settings.database.path`
2. Create `connection_db_manager_t`, `attach()` to router
3. Create `device_registry_t`, `attach()`, `seed_ini()`
4. Inject `referenced_queries_from(conn)` as the pruning provider
5. `schedule_device_registry_cleanup()` on `cron_tasks_t`

## Related docs

- [entities.md](entities.md) — identity/query grammar
- [frontend.md](../development/frontend.md) — Web UI connection editor
- [control-protocol.md](../development/control-protocol.md) — RPC
- [control-protocol.md](../development/control-protocol.md) — control socket command reference

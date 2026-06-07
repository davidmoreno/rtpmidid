# Control protocol (JSON-RPC)

Unix domain socket (default `/var/run/rtpmidid/control.sock`) and WebSocket
`/ws` share the same line-delimited JSON-RPC API.

User-oriented summary: [user/control.md](../user/control.md).

Implementation: [`src/control_rpc.cpp`](../../src/control_rpc.cpp),
[`src/control_socket.cpp`](../../src/control_socket.cpp). Types:
[`src/dm_json_rpc.hpp`](../../src/dm_json_rpc.hpp).

## Transport

| Direction | Format |
|-----------|--------|
| Request | `{"method":"<name>","params":{...},"id":<optional>}` |
| Success | `{"id":…,"result":…}` |
| Error | `{"id":…,"error":"<message>"}` |
| Parse fail | `{"error":"<message>"}` |

`params` defaults to `{}`. Many mutations return `{"result":["ok"]}`.

## Connect APIs — which to use

| API | When |
|-----|------|
| **`router.connect`** | Two peers already exist; connect by numeric `peer_id`. Unidirectional. |
| **`router.disconnect`** | Remove a router edge between two `peer_id`s. |
| **`endpoint.connect`** | High-level: sides are **identity strings**. Creates peers if needed. ALSA↔ALSA may use kernel `aconnect`; mixed types use router. Optional `bidi`. |
| **`endpoint.disconnect`** | Tear down by identity strings (router and/or `aconnect`). |
| **`connections.save`** | Persist a pair for **auto-restore** across restarts (`side_a`, `side_b`, `direction`, `enabled`). Requires `[database]`. |
| **`connections.enable`** / **`disable`** | Toggle auto-reconnect without deleting the row. |
| **`connect`** | Convenience: create `alsa_listener` for remote `{hostname, port?, name?}`. |

Typical Web UI flow: `endpoint.connect` for immediate routing;
`connections.save` to remember it.

## Commands

| Method | Params (summary) | Notes |
|--------|------------------|-------|
| `status` | `{}` | Full daemon snapshot |
| `help` | `{}` | Command list |
| `connect` | `{hostname, port?, name?}` | Remote RTP-MIDI |
| `router.remove` | `{peer_id}` | |
| `router.connect` | `{from, to}` | peer IDs |
| `router.disconnect` | `{from, to}` | |
| `router.create` | `{identity}` | `type:key=value,…` string |
| `router.create.list` | `{}` | Creatable type prefixes |
| `endpoint.connect` | `{from, to, bidi?}` | identity strings |
| `endpoint.disconnect` | `{from, to}` | |
| `mdns.remove` | `{name, hostname?, port}` | |
| `midi.listAlsaSeq` | `{}` | |
| `midi.listAlsaSubscriptions` | `{}` | `aconnect` links |
| `midi.listRawMidi` | `{}` | |
| `monitor.start` | `{identity}` | → `{uuid, peer_id, target_peer_id}` |
| `monitor.stop` | `{uuid}` | |
| `devices.list` | `{}` | needs `[database]` |
| `devices.add_manual` | `{identity, name?}` | |
| `devices.remove` | `{identity}` | |
| `connections.list` | `{}` | |
| `connections.save` | `{side_a, side_b, direction, enabled}` | `direction`: `a2b`\|`b2a`\|`both` |
| `connections.add` | `{side_a, side_b}` | legacy `both` |
| `connections.remove` | `{side_a, side_b}` | |
| `connections.enable` | `{side_a, side_b}` | |
| `connections.disable` | `{side_a, side_b}` | |

**Not supported:** `stats` (use `status`), `quit`/`exit`, bare `create`.

Param structs: `src/dm_json_rpc.hpp`. Handler list: `build_help_entries()` in
`control_rpc.cpp`.

## Peer commands

`{peer_id}.{subcmd}` — e.g. `5.status`. All peers: `help`, `status`.
`peer_import_alsa_rtp_t`: `add_endpoint`, `remove_endpoint` (`{hostname, port}`).

## Async events

The daemon pushes server-sent events over the WebSocket `/ws` when the client
has subscribed to event channels via the `subscribe` RPC method.

### Subscription

| Method | Params | Notes |
|--------|--------|-------|
| `subscribe` | `{channels: ["router.peer_added", ...]}` | Start receiving events |
| `unsubscribe` | `{channels: ["router.peer_added"]}` | Stop specific channels |

Event format:
```json
{"event":"<channel>","params":<payload>}
```

### Event channels

| Channel | Payload | Trigger |
|---------|---------|---------|
| `router.peer_added` | `router_peer_row_t` (full peer row) | New peer added to router |
| `router.peer_removed` | `{"peer_id": uint64}` | Peer removed from router |
| `router.peer_updated` | `router_peer_row_t` (full peer row) | Peer stats/status changed (periodic, ~500ms) |
| `router.edge_added` | `{"from": uint64, "to": uint64}` | Router connection created |
| `router.edge_removed` | `{"from": uint64, "to": uint64}` | Router connection removed |
| `mdns.discovered` | `mdns_remote_row_t` | New mDNS remote discovered |
| `mdns.removed` | `{"name": string, "address": string, "port": uint32}` | mDNS remote removed |
| `mdns.announcement_changed` | `mdns_snapshot_t` (full state) | Own announcements or remotes changed (periodic) |

Legacy server-initiated close events (still sent regardless of subscription):

```json
{"event":"close","detail":"Shutdown","code":0}
```

| Code | Detail |
|------|--------|
| 0 | Shutdown / disconnect |
| 1 | Message too long (~1023 bytes) |

## Implementation notes

1. Control thread uses blocking `poll()` — not on MIDI poller thread.
2. `control_rpc_dispatch_line()` shared with WebSocket.
3. Topology via `enqueue_*` on router thread.
4. `status_rows()` — LOW-priority router query + `reply_channel_t`.

JSON stack: [dm-json.md](dm-json.md). Web UI: [frontend.md](frontend.md).

## jq example

```sh
rtpmidid-cli status | jq -r '["id","name","type"],(.result.router[]|[.id,.name,.type]) | @csv'
```

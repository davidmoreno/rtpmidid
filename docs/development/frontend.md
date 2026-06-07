# Frontend / Web UI

The Web UI is a Preact SPA served by the daemon when `[web]` is enabled. It uses
the **same JSON-RPC protocol** as the Unix control socket.

## Server side

[`src/web_server.cpp`](../../src/web_server.cpp) (httplib):

| Route | Purpose |
|-------|---------|
| `/` + static assets | SPA from `settings.web.root` (default `frontend/dist`) |
| `/ws` | JSON-RPC over WebSocket → `control_rpc_dispatch_line()` |
| `/ws/monitor?uuid=…` | Binary WebSocket frames of raw MIDI bytes |

HTTP Basic auth when `[web] username` / `password` are set (applies to `/ws` and
monitor WS).

Configuration: `[web]` in INI — see [default.ini](../../default.ini) and
[configuration.md](../development/configuration.md).

## Client entry

| File | Role |
|------|------|
| `frontend/src/index.tsx` | Mount point |
| `frontend/src/app.tsx` | Root: tabs, RPC client, event subscription, hash routing |
| `frontend/src/rpc.ts` | `RpcClient` — WebSocket JSON-RPC with reconnect and subscriptions |
| `frontend/src/store.ts` | Event-sourcing store — receives snapshot + incremental events from WebSocket |

### Event-driven architecture

The Web UI no longer polls for status. On connect:
1. Subscribes to event channels (`router.peer_added`, `router.peer_removed`,
   `router.edge_added`, `router.edge_removed`, `mdns.discovered`, `mdns.removed`)
2. Fetches the initial `status` snapshot once
3. Incremental events from the WebSocket are reduced into the `daemonStore`
   (event-sourcing pattern in `store.ts`)
4. Components use `useDaemonState()` hook to re-render on store changes

Auxiliary data (ALSA MIDI lists, connection DB, device registry) is fetched
on-demand when the relevant tab is opened.

Backend events are pushed from router/mDNS signals through a per-connection
`event_subscription_manager_t` (`src/event_subscription.hpp`).

Fullscreen MIDI monitor: hash route `#monitor?uuid=…` →
`MidiMonitorStandalone` (bypasses main tab chrome).

## Tabs

| Tab | File | Data sources |
|-----|------|--------------|
| Devices | `tabs/DevicesTab.tsx` | `status` + `devices.list` merged via `mergeDeviceList.ts` |
| Connections | `tabs/ConnectionsTab.tsx` | `connections.list`, `ConnectionEditorDialog` |
| Peers | `tabs/PeersTab.tsx` | `status` router rows |
| mDNS | `tabs/MdnsTab.tsx` | `status` mDNS snapshot |
| Actions | `tabs/ActionsTab.tsx` | Connect/disconnect/monitor actions |
| Settings | `tabs/SettingsTab.tsx` | Theme appearance |
| About | `tabs/AboutTab.tsx` | Version info |

## Key modules

### Identity grammar (shared with backend)

`frontend/src/deviceIdentity.ts` — parse/serialize `type:key=value` strings,
field labels, stored-query bracket toggles. Tests: `deviceIdentity.test.ts`.

Must stay aligned with [`src/device_identity.cpp`](../../src/device_identity.cpp).

### Device list merge

`frontend/src/mergeDeviceList.ts` — combines live endpoint cards from `status`
with offline registry rows from `devices.list`. Tags: Online/Offline, source
(Discovered, Config, Manual, Session).

### Persisted connections

- `persistedConnections.ts` — parse `connections.list` result
- `persistedConnectionsFormat.ts` — serialize for `connections.save`
- `StoredQueryEditor.tsx` — per-field active/`[ ]` toggles
- `ConnectionEditorDialog.tsx` — direction picker, endpoint picker

### Endpoints and connect

`frontend/src/endpoints.ts` — high-level connect/disconnect via
`endpoint.connect` / `endpoint.disconnect`.

Pure ALSA pairs use kernel `aconnect` (detected by `isDirectAlsaSide()` in
`connections.alsa.test.ts` logic). Mixed pairs use router peers.

### MIDI monitor

1. `monitor.start` RPC → `{uuid, peer_id, target_peer_id}`
2. Open `/ws/monitor?uuid=…` (binary frames)
3. `midiMonitorParse.ts` decodes raw bytes for display
4. Fullscreen: `#monitor?uuid=…`

Monitor sink peer: `webui_midi_monitor_peer_t` — only sees MIDI that flows
through router edges (not pure `aconnect` unless tap is active).

### Peer creation

`frontend/src/rpcRouterCreate.ts` — calls `router.create` with `{identity}`.

## Build

```bash
# Via main Makefile (before run/install)
make build   # does not always rebuild frontend

# Explicit frontend build
./packaging/build-frontend.sh
# Output: frontend/dist/
```

Packages embed assets at `/usr/share/rtpmidid/html` (see `packaging/README.md`).

Development: set `[web] root=frontend/dist` and rebuild frontend after changes.

## Tests

Frontend unit tests (vitest): `frontend/src/*.test.ts`

Run from `frontend/` directory per project setup, or via CI packaging scripts.

## Related docs

- [control-protocol.md](control-protocol.md) — RPC protocol
- [entities.md](../architecture/entities.md) — identity grammar
- [database.md](../architecture/database.md) — persisted connections
- [dm-json.md](dm-json.md) — status JSON shapes
- [web-ui (user)](../user/web-ui.md) — user-facing guide

## Why

Every outbound rtpmidi connection is initiated eagerly: `[connect_to]` entries connect at daemon startup and every mdns-discovered server connects the moment it is discovered — even when nothing locally wants the session. The pre-actor daemon (removed in the actor refactor) connected lazily: an ALSA port waited for a subscription and only then created the network session. The refactor also left rawmidi devices opened (exclusive access) at startup, never implemented the parsed `alsa_hw_auto_export` setting, and never announced `[rtpmidi_announce]` sections over mDNS. Result: idle daemons hold live network sessions and opened devices for nothing, and a whole class of "export a local device" features is missing.

## What Changes

- **Lazy outbound connections**: `[connect_to]` sections and mdns-discovered servers become *waiting ALSA ports*. The rtpmidi session is created only when an ALSA client subscribes to the port, and torn down on the last unsubscribe.
- **Waiting ports are not router peers**: a waiting port is just a seq port owned by the ALSA listener. The first subscription registers it as a real hosted peer; the last unsubscribe unregisters it.
- **One session per remote pair**: an existing session (either direction) is reused instead of opening a duplicate connection; when a new connection for an already-connected remote arrives, the new connection replaces the old one.
- **New global `rtpmidi_server` actor** (not a peer): owns all listen sockets (announce sections + one per exported device), announces exports over mDNS, holds the export registry, and on inbound connection creates the real peer pair and wires it.
- **Per-device export ports**: each exported device (rawmidi, auto-exported ALSA seq port) gets its own listen port and announce name, so a remote picks what it wants by choosing the port.
- **rawmidi deferred open**: server-mode rawmidi devices are registered as `{name, path}` only — the device is **never opened while idle** (exclusive access). First inbound connection opens it, spawns the rawmidi peer, wires it to the acceptor; closing the connection closes the device. Client-mode (`hostname=` set) keeps today's eager behavior (device dedicated to a remote server).
- **`alsa_hw_auto_export` implemented**: the ALSA listener enumerates local ports at start (type + regex filters from the ini, **excluding the daemon's own ports**), keeps the list updated via port add/remove announcements, and creates per-device export servers for each match.
- **Status gains an `exports` section** (additive wire change): waiting/exported endpoints from the two server actors — waiting ports and their remote targets, rawmidi registry, auto-exported seq ports. The router's peer list shows live sessions only.

## Capabilities

### New Capabilities

- `on-demand-connections`: lazy ALSA-triggered outbound sessions for `[connect_to]` and mdns-discovered remotes; waiting ports; on-demand peer registration; one-session-per-remote-pair reuse (new connection replaces old); teardown on last unsubscribe.
- `rtpmidi-export-server`: the global rtpmidi server actor: owns all listen sockets, announces exports over mDNS, per-device ports, inbound connections create the real peer pair (generic Network server, rawmidi, auto-exported seq) and wire it.
- `rawmidi-export`: server-mode rawmidi devices registered without opening; device opened only when a connection needs it; closed on disconnect; client mode (`hostname=` set) keeps eager behavior.
- `alsa-listener`: the ALSA-side actor: waiting ports, on-demand registration as router peers, subscribe/unsubscribe events, per-connection ports, auto-export enumeration + add/remove tracking with ini filters and no-own-ports exclusion.

### Modified Capabilities

- `control-socket-jsondm`: the daemon status response gains an additive `exports` section (waiting/exported endpoints from the listener and server actors); existing shapes unchanged.

## Impact

- **Code**: `src/main.cpp` (actor graph + static setup), `src/alsa_actor.*` → ALSA listener semantics, new `src/rtpmidi_server_actor.*`, `src/mdns_actor.*` (discovery creates ports only; announce wiring), `src/network_rtpmidi_listener_actor.*` (folded into the server), `src/local_rawmidi_peer_actor.*` (deferred open), `src/control_socket_actor.*` (exports gather), `src/peer_status.*` (dead listener status types removed or repurposed), `cli/rtpmidid-cli.py` (exports display). The `network_rtpmidi_peer_actor` protocol engine is unchanged.
- **Tests**: `tests/test_alsa_bridge.cpp` and `tests/test_network_actor.cpp` assert today's eager behavior and must be rewritten; new tests for lazy subscribe/teardown, session reuse, deferred rawmidi open, and auto-export.
- **Docs**: `default.ini` comments, `docs/CONTROL.md`, README behavior notes.
- **No breaking config or wire changes**: all settings keys stay; the status change is additive.

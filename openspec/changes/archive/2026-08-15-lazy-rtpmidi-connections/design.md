## Context

The actor refactor (`add-actor-architecture`) ported the old single-threaded daemon to actors but dropped the old lazy-connection model. Today:

- `main.cpp::setup_static_peers` spawns a `network_rtpmidi_peer_actor_t` **initiator** for every `[connect_to]` entry at startup — eager DNS, bind, IN/CK handshake, reconnect loop, no ALSA port, no router wiring.
- `mdns_actor_t::on_discovered` creates an ALSA port **and** spawns the network client **and** wires them, all eagerly — every discovered server holds a live session even with zero ALSA subscribers.
- `network_rtpmidi_listener_actor_t` (one per `[rtpmidi_announce]` section) creates a **new** ALSA port per accepted connection; duplicate sessions to the same remote are possible.
- rawmidi devices are `open()`ed at startup (exclusive access held forever); `alsa_hw_auto_export` is parsed but unimplemented; `[rtpmidi_announce]` sections never announce over mDNS.
- `peer_status.hpp` still carries dead listener status types (`alsa_listener_status_t`, `alsa_multi_listener_status_t`, `rtp_multi_listener_status_t`).

The old pre-actor code (`local_alsa_listener_t`, `local_alsa_multi_listener_t`, `HwAutoAnnounce`) is in git history and is the reference for the intended behavior.

## Goals / Non-Goals

**Goals:**
- No rtpmidi session exists until something locally needs it: `[connect_to]` and discovered remotes become waiting ALSA ports; sessions start on first ALSA subscription and end on the last unsubscribe.
- Waiting ports are not router peers — the router registry is exactly the set of live sessions.
- One session per remote pair (RTP-MIDI sessions are bidirectional); duplicate connections converge by "new replaces old".
- A global `rtpmidi_server` actor owns all listen sockets, announces exports over mDNS, and creates the real peer pair on every inbound connection.
- Per-device export ports: each exported device (rawmidi, auto-exported seq port) is independently connectable and announced.
- Server-mode rawmidi devices are never opened while idle (exclusive access); opened on connection, closed on disconnect. Client-mode rawmidi (`hostname=` set) stays eager.
- `alsa_hw_auto_export` implemented: enumerate local ports (type + regex filters, no own ports), track add/remove, export each match.
- Status gains an additive `exports` section; existing wire shapes unchanged.

**Non-Goals:**
- No change to the `network_rtpmidi_peer_actor` protocol engine (rtppeer handshake/keepalive stays as-is).
- No change to the router's midi-routing contract (spawn/register/connect/remove are reused as-is).
- No config key removals; rawmidi client mode behavior unchanged.
- No new external dependencies.

## Decisions

### D1: Two global server actors, neither a router peer
`alsa_listener` (evolves from `alsa_actor`) owns the seq client, all ports, subscription events, and auto-export tracking. `rtpmidi_server` (new; replaces the per-section `network_rtpmidi_listener_actor`s) owns every listen socket, the export registry, mDNS announces, and session bookkeeping. Neither registers itself with the router.

- *Alternatives considered*: (a) per-remote listener peers (the old design) — reintroduces placeholder peer lifecycles and dead status types; (b) keep eager clients — the bug; (c) one combined actor — couples ALSA and network worlds.
- *Rationale*: matches the explored architecture; peers exist only for real connections; each world's concerns stay in one owner; session reuse bookkeeping is centralized where accept happens.

### D2: Waiting ports are not registered with the router
A waiting port (discovered remote, `connect_to`, export port) is just a seq port owned by `alsa_listener`. The first ALSA subscription registers it as a hosted peer (`register_peer_t`); the last unsubscribe unregisters it (`unregister_peer_t`).

- *Alternatives considered*: always-registered hosted ports (today's model) — pollutes the router registry and status with non-sessions.
- *Rationale*: "none of them is really a peer until connected"; the router registry stays small and meaningful; the CLI routes tab shows live sessions only, with waiting exports in the new `exports` status section.

### D3: One session per remote pair, new replaces old
Sessions are keyed by remote identity — the remote's announced name (inbound: from the IN packet; outbound: the section/discovery name). Where a full discovered name (`hostname:port - service`) is available it is used for matching to reduce collisions. Rules:

- An ALSA subscription to a waiting port reuses an existing inbound session with the same remote instead of spawning a duplicate outbound client.
- An inbound connection whose remote already has a session (inbound or outbound) replaces the old session: the old peer is removed and the port is rewired to the new acceptor. When the port has active ALSA subscribers, the rewire keeps the port registered and wired through the switch.
- Each side keeps exactly one session with a given remote. A transient
  both-sides-open race leaves one session per side; the spec defines no
  collision-resolution mechanism (see Open Questions), so no cross-pair
  tiebreaker is implemented — the double session is harmless (sessions
  are bidirectional) and converges when either association closes.

- *Alternatives considered*: (a) per-direction sessions (today) — duplicate sessions and ports; (b) first-session-wins — the second side's export stays dark.
- *Rationale*: RTP-MIDI sessions are bidirectional, so one session serves both directions; deterministic convergence.

### D4: Per-device export ports on the rtpmidi server
Each exported device — every `[rtpmidi_announce]` section (generic "Network"), every server-mode rawmidi device, every auto-exported seq port — gets its own listen socket (P and P+1) and mDNS announce name on the server. Configured ports (`port` for announce sections, `local_udp_port` for rawmidi/`connect_to` sections) are honored; other exports get assigned ports. The remote selects the device by choosing the port, exactly like today's `local_udp_port` model.

- *Alternatives considered*: shared port wired to all exports — the server cannot know which device a connection wants (the IN packet carries the initiator's name, not the target).
- *Rationale*: unambiguous routing; per-device announce names make exports discoverable; scales with the (small) export count.

### D5: rawmidi deferred open; client mode eager
Server-mode rawmidi is registered in the export registry as `{name, device path}` — no fd. On inbound connection the device is opened (`open(path, O_RDWR|O_NONBLOCK)`), the rawmidi peer is spawned (the peer owns the fd, so no peer = no open), and wired to the acceptor. Close drops the peer and the fd. Client-mode rawmidi (`hostname=` set) keeps today's behavior: opened and connected at startup — an explicit "this device is dedicated to that server" config.

- *Alternatives considered*: open-at-startup for all rawmidi (today) — holds exclusive access while idle, blocking other programs; ALSA-gated client mode — no natural trigger exists for a raw device.
- *Rationale*: exclusive-access devices must not be held while idle; client mode is a deliberate dedication.

### D6: Auto-export lives in `alsa_listener`; export servers are created through `rtpmidi_server`
The listener enumerates ports (`for_devices`/`for_ports`), applies the ini filters (`alsa_hw_auto_export.type` + name regexes), excludes the daemon's own client ports ("no own ports"), subscribes to the announce port to track add/remove events, and for each match asks the server to create a per-device export (listen socket + announce + registry entry).

- *Alternatives considered*: enumeration inside `rtpmidi_server` — mixes ALSA enumeration into the network actor.
- *Rationale*: port enumeration and subscription tracking are ALSA-side concerns; the server stays network-pure and just consumes export create/remove requests.

### D7: Announcements owned by `rtpmidi_server`
The server posts `mdns_announce_t`/`mdns_unannounce_t` for every export (announce sections, rawmidi names, auto-exported port names) — closing today's gap where `[rtpmidi_announce]` sockets existed but were never announced.

- *Rationale*: announcements mirror the listen-socket inventory, which the server owns; keeps `mdns_actor` as a pure avahi wrapper.

### D8: Message flows
```
Outbound (lazy):
  ALSA subscribe on waiting port R
    alsa_listener: register_peer_t(port) → router
    alsa_listener → rtpmidi_server: session_request{port, remote, subscribed}
    rtpmidi_server: if inbound session for R exists → connect port ↔ session
                    else spawn_peer_t(client to R) → router; connect port ↔ client
  ALSA unsubscribe (last):
    rtpmidi_server: if session shared with inbound → just unregister port
                    else remove_peer_t(client) → router; unregister_peer_t(port)

Inbound:
  IN packet on server socket P/P+1
    rtpmidi_server: parse remote name; match against known remotes + exports
      - known remote R with a waiting port → reuse port (no new ALSA port)
      - generic Network server → alsa_listener: alsa_create_port_t(name) → new port
      - exported rawmidi → open device, spawn rawmidi peer
      - auto-exported seq → alsa_listener: create subscription peer to the local port
    spawn_peer_t(acceptor) → router; connect port/acceptor both ways
    reused waiting port → register_peer_t(port) while the session is live
      (unregister on session end if it has no ALSA subscribers)
    if an older session with the same remote exists → remove_peer_t(old)

Discovery:
  mdns → alsa_listener: create waiting port (no client)
  mdns remove → alsa_listener: remove port
```

## Risks / Trade-offs

- [Remote-name collisions (two servers with the same announced name)] → Sessions key by the remote's announced name; the first port wins and later collisions are logged; matching uses the full discovered name (`hostname:port - service` where available) to reduce ambiguity.
- [Exclusive rawmidi open fails at connection time (device busy)] → The connection is rejected/removed with an error log; the next connection retries; the registry entry stays so the device becomes available when free.
- [Both-sides-open race leaves two sessions across the pair] → Each side keeps exactly one session with the remote. A transient double session (one per side) is harmless: RTP-MIDI sessions are bidirectional, so MIDI flows correctly over either association; the double session converges naturally when either side closes one association. Logged when detected.
- [Session replacement while the port has active ALSA subscribers] → The rewire keeps the port registered and wired through the switch (wire the new acceptor before removing the old peer); a brief midi gap during the switch is acceptable and logged.
- [Behavior change for users relying on always-on connections] → Documented in `default.ini` and README: an ALSA subscriber is now required to start a session.
- [Auto-export socket count grows with local ports] → Per-device ports are created only for ports matching the (usually narrow) ini filters; removed when the port disappears.
- [Registration/unregistration churn on the router] → register/unregister are O(1) map mutations (existing contract); subscription events are the only trigger.

## Migration Plan

Config is unchanged; no wire break (status change is additive). The implementation is a single change with ordered tasks; rollback is a revert of the change's commits. Sequencing inside the change:

1. Waiting-port semantics + lazy outbound for `connect_to` and discovery (the eager-startup and eager-discovery connection bugs) — smallest shippable slice.
2. `rtpmidi_server` actor: sockets, accept → pair creation, announce, session reuse/replacement.
3. rawmidi deferred open + export registry.
4. `alsa_hw_auto_export` + per-device seq exports.
5. `exports` status section + CLI display; test rewrites throughout.

## Open Questions

- ~~Pair-convergence tiebreaker for simultaneous both-sides-open~~ —
  **Resolved (checked against the Apple MIDI Network Driver Protocol, the
  de-facto RTP-MIDI session protocol):** the spec defines no behavior for
  simultaneous invitations and no collision resolution; the initiator
  token is a random value used only to match `IN` → `OK`/`NO`, so a
  cross-pair tiebreaker (e.g. "lower initiator id") cannot be negotiated
  with conforming remotes. Decision: the implemented per-side guarantee
  (at most one session per remote, new connection replaces the old one)
  is the strongest guarantee achievable without a non-standard extension;
  a transient one-session-per-side race is harmless (sessions are
  bidirectional) and left to converge when either association closes.
  (lazy-rtpmidi-connections, task 4.4)

# Tasks

## 1. ALSA listener groundwork

- [x] 1.1 Rename/evolve `alsa_actor_t` into the ALSA listener role: keep one actor owning the seq client and all ports; ensure it is never registered as a router peer itself
- [x] 1.2 Add waiting-port semantics: ports created by discovery/`connect_to` are created unregistered; add `subscribe`/`unsubscribe` tracking per port with a subscriber count
- [x] 1.3 On-demand registration: first subscription posts `register_peer_t`, last unsubscribe posts `unregister_peer_t` for waiting ports
- [x] 1.4 Add a message from the listener to the `rtpmidi_server_actor_t` (`session_request{port, remote, subscribed}`) replacing the current `alsa_port_event_t` → mdns wiring
- [x] 1.5 Update `tests/test_alsa_bridge.cpp`: waiting ports unregistered while idle; registered on subscribe; unregistered on last unsubscribe

## 2. Lazy outbound sessions (eager `[connect_to]` startup and eager discovery connections)

- [x] 2.1 `mdns_actor_t::on_discovered` creates a waiting port only (remove the eager client spawn + wiring); `on_removed` removes the port
- [x] 2.2 `main.cpp::setup_static_peers`: `[connect_to]` sections create waiting ports instead of eager initiator peers
- [x] 2.3 Subscription handler: spawn the initiator client (`network_rtpmidi_peer_actor_t`, DNS on worker, `local_udp_port` when set) and wire port ↔ client both ways
- [x] 2.4 Unsubscribe handler: last unsubscribe unregisters the port; remove the client (stop/join via router `remove_peer_t`) unless the session is shared with an inbound connection (then unregister the port only)
- [x] 2.5 No session while waiting: assert no sockets/IN/CK for an idle waiting port (test with a connect_to port and a discovered remote; the live smoke check is 8.3)
- [x] 2.6 Update `tests/test_network_actor.cpp` and `tests/test_alsa_bridge.cpp` discovery tests to the lazy model; add subscribe→spawn and unsubscribe→teardown tests

## 3. rtpmidi server actor

- [x] 3.1 New `rtpmidi_server_actor_t` owning all listen sockets (control P / midi P+1 per `[rtpmidi_announce]` section and per export, honoring configured `port`/`local_udp_port` settings), with per-socket datagram routing like the current listener actor
- [x] 3.2 Accept flow: parse remote name from the IN packet, spawn the acceptor peer via `spawn_peer_t`, create/reuse the ALSA port, wire both ways; when a waiting port is reused, keep it registered as a hosted peer for the lifetime of the inbound session (unregister on session end if it has no ALSA subscribers)
- [x] 3.3 mDNS announce: post `mdns_announce_t`/`mdns_unannounce_t` for every listen endpoint (fixes the announce gap); wire the server mailbox to mdns
- [x] 3.4 Remove `network_rtpmidi_listener_actor_t` and its per-section instantiation in `main.cpp`; route everything through the server actor
- [x] 3.5 Keep the foreign-datagram re-forwarding path (misdelivered datagrams re-routed to the server) working
- [x] 3.6 Port `tests/test_network_actor.cpp` listener tests to the server actor; add accept→pair-created tests

## 4. One session per remote pair

- [x] 4.1 Session bookkeeping in the server: map remote identity → session (peer id + direction + initiator id)
- [x] 4.2 Reuse on subscribe: a subscription to a waiting port whose remote has an inbound session wires the port to that session and spawns no client
- [x] 4.3 Replace on inbound: an inbound connection for a remote that already has a session removes the old peer and rewires the port to the new acceptor; with active ALSA subscribers the rewire is atomic (port stays registered and wired through the switch)
- [x] 4.4 Pair convergence: keep at most one session per remote on this side; implement the cross-pair tiebreaker (lower initiator id) once its mechanism is confirmed against the RTP-MIDI spec (design Open Questions)
- [x] 4.5 Tests: reuse (subscribe with existing inbound → no duplicate client), replace (inbound replaces outbound; duplicate inbound replaced), tiebreaker

## 5. rawmidi export with deferred open

- [x] 5.1 Export registry in the server: entries `{name, kind, target}` for rawmidi and seq exports, with create/remove messages
- [x] 5.2 Server-mode rawmidi: register `{name, device path}` at startup, create its listen socket pair (honoring `local_udp_port` when set) + announcement, open nothing
- [x] 5.3 Connection flow: on inbound connection open the device (`O_RDWR|O_NONBLOCK`), spawn the rawmidi peer owning the fd, wire to the acceptor
- [x] 5.4 Disconnect flow: remove the rawmidi peer and close the fd; open failure removes the connection with an error log and keeps the export registered
- [x] 5.5 Client-mode rawmidi (`hostname=` set) keeps today's eager open + outbound connect at startup
- [x] 5.6 Tests: device not opened while idle (fd check), opened on connection, closed on disconnect, open-failure rejected, client mode eager

## 6. ALSA hardware auto-export

- [x] 6.1 Enumerate local sequencer ports at start; filter by `alsa_hw_auto_export` type + name regexes; exclude the daemon's own client ports
- [x] 6.2 Subscribe to the sequencer announce port (0:1) for port add/remove events; create/remove exports accordingly
- [x] 6.3 Per-device seq exports: for each matching local port, create a listen socket pair + announcement on the server and a per-connection seq subscription peer on connection
- [x] 6.4 Tests: matching ports exported, own ports excluded, hotplug add/remove tracked, connection creates the seq subscription peer pair

## 7. Status exports section and CLI

- [x] 7.1 Status gather: `exports` object from the listener (waiting ports + targets, states) and the server (Network servers, rawmidi entries, seq exports)
- [x] 7.2 Wire into `control_socket_actor` status response as an additive `exports` section; keep `router`/`mdns` shapes byte-identical
- [x] 7.3 Remove dead listener status types from `peer_status.*` if nothing emits them
- [x] 7.4 CLI: display the `exports` section (e.g. in the mDNS tab or a dedicated view); keep routes tab = live sessions
- [x] 7.5 Tests: status contains exports with expected entries; existing status shapes byte-identical

## 8. Docs, config comments, and final pass

- [x] 8.1 Update `default.ini` comments: lazy connections need an ALSA subscriber; rawmidi server mode never holds the device while idle; auto-export behavior
- [x] 8.2 Update `docs/CONTROL.md` and README behavior notes (no always-on outbound sessions; exports status section)
- [x] 8.3 Full build + test suite green (`ctest`), daemon smoke test: idle daemon holds no sessions, `aconnect` shows waiting ports, subscription starts a session
- [x] 8.4 `openspec validate` for the change passes and the archive step is prepared

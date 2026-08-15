# rtpmidi Export Server Specification

## Purpose

The global rtpmidi server actor: owns every listen socket, announces exports over mDNS, holds the export registry, and on inbound connection creates the real peer pair and wires it — one session per remote pair.

## ADDED Requirements

### Requirement: Global server owns all listen sockets and announcements
A single rtpmidi server actor SHALL own every rtpmidi listen socket: one per `[rtpmidi_announce]` section (the generic "Network" server) and one per exported device (rawmidi, auto-exported seq port). Each listen socket pair (control P and midi P+1) SHALL be announced over mDNS with its export name and port. The server SHALL NOT be registered as a router peer.

#### Scenario: Announce sections are announced
- **WHEN** the daemon starts with `[rtpmidi_announce]` sections
- **THEN** each section's listen sockets exist and an mDNS announcement for each name/port is posted

#### Scenario: Exported devices are announced
- **WHEN** an export (rawmidi or auto-exported seq port) is registered
- **THEN** a listen socket pair and an mDNS announcement for its name are created

### Requirement: Generic Network server creates a per-connection pair
An inbound connection to a generic "Network" server SHALL create a new ALSA port named after the remote and an acceptor peer, and SHALL wire them together. When the connection's remote name matches a known waiting port (a discovered or `connect_to` remote), the existing waiting port SHALL be reused instead of creating a new ALSA port.

#### Scenario: Unknown remote gets a fresh port
- **WHEN** a connection arrives on a generic Network server from an unknown remote
- **THEN** a new ALSA port and an acceptor peer are created and wired

#### Scenario: Known remote reuses its waiting port
- **WHEN** a connection arrives from a remote that has a waiting port
- **THEN** the waiting port is wired to the acceptor and no new ALSA port is created

### Requirement: Per-device export ports
Each exported device (rawmidi, auto-exported seq port) SHALL be reachable through its own listen socket pair, so a remote selects the device by connecting to its port. Configured ports (the `port` setting of an announce section, the `local_udp_port` setting of a rawmidi section) SHALL be honored for the corresponding socket pair. An inbound connection to a device export SHALL create the device's real peer pair: for rawmidi, open the device and spawn the rawmidi peer; for an auto-exported seq port, create the seq subscription peer. The pair SHALL be wired to the acceptor.

#### Scenario: rawmidi export connection
- **WHEN** a connection arrives on a rawmidi export's port
- **THEN** the device is opened, the rawmidi peer and acceptor peer are created and wired

#### Scenario: seq export connection
- **WHEN** a connection arrives on an auto-exported seq port
- **THEN** a seq subscription peer for the local port and an acceptor peer are created and wired

### Requirement: Waiting ports wired to inbound sessions are registered
When a waiting port is reused for an inbound session, the port SHALL be registered as a hosted peer for the lifetime of that session, even with zero ALSA subscribers, so the router registry reflects exactly the live sessions. When the inbound session ends, the port SHALL be unregistered if it has no ALSA subscribers and SHALL remain available as a waiting port.

#### Scenario: Inbound session registers a reused waiting port
- **WHEN** an inbound connection is wired to a waiting port with no ALSA subscribers
- **THEN** the port is registered as a hosted peer while the session is live

#### Scenario: Session end with no subscribers
- **WHEN** an inbound session on a reused waiting port ends and the port has no ALSA subscribers
- **THEN** the port is unregistered and remains a waiting port

### Requirement: New connection replaces an existing session
When an inbound connection's remote already has a session (inbound or outbound), the new connection SHALL become the session: the older session's peer SHALL be removed and the port SHALL be rewired to the new acceptor. Each side SHALL keep at most one session with a given remote. When both sides open simultaneously, the transient double session SHALL be resolved so the pair converges to a single session; the deterministic tiebreaker (candidate: keep the session with the lower initiator id) is recorded as an open question in the design pending confirmation against the RTP-MIDI spec.

#### Scenario: Inbound replaces outbound
- **WHEN** an inbound connection arrives from a remote that has an outbound session
- **THEN** the outbound client is removed and the port is wired to the new acceptor

#### Scenario: Replacement with active subscribers
- **WHEN** an inbound connection replaces a session whose port has active ALSA subscribers
- **THEN** the port stays registered and is rewired to the new acceptor without being unregistered

#### Scenario: Duplicate inbound replaced
- **WHEN** a second inbound connection arrives from a remote that already has an inbound session
- **THEN** the first session is removed and the port is rewired to the second acceptor

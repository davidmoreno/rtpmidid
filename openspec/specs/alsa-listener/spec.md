# ALSA Listener Specification

## Purpose

The ALSA-side gateway actor: owns the seq client and every port, registers ports with the router only on demand, and tracks local hardware/software ports for auto-export with ini filters and no-own-ports exclusion.

## Requirements

### Requirement: Listener owns all ALSA ports
A single ALSA listener actor SHALL own the seq client and SHALL create every seq port the daemon creates: waiting ports (discovered remotes, `connect_to` sections) and per-connection ports for generic Network sessions. Exports add no daemon ports of their own: rawmidi exports have no seq port, and auto-exported local ports are consumed in place via subscription peers. The listener SHALL NOT be registered as a router peer.

#### Scenario: All ports from one owner
- **WHEN** the daemon creates ALSA ports of any kind
- **THEN** they are created by the ALSA listener actor on its seq client

### Requirement: Ports register on demand
A waiting port SHALL NOT be registered with the router while unsubscribed and without a live session. The first ALSA subscription SHALL register the port as a hosted peer (`register_peer`); the last unsubscribe SHALL unregister it unless a live inbound session is wired to it. A waiting port wired to a live inbound session SHALL stay registered for the lifetime of that session. Per-connection ports created for generic Network sessions SHALL be registered when created and removed and unregistered when their session ends.

#### Scenario: Waiting port not registered
- **WHEN** a waiting port has no ALSA subscribers and no live session
- **THEN** it has no router peer registration

#### Scenario: Subscription registers the port
- **WHEN** the first ALSA client subscribes to a waiting port
- **THEN** the port is registered as a hosted peer with the router

#### Scenario: Last unsubscribe unregisters the port
- **WHEN** the last ALSA client unsubscribes from a registered port with no live session
- **THEN** the port is unregistered from the router

#### Scenario: Last unsubscribe keeps a live inbound session registered
- **WHEN** the last ALSA client unsubscribes from a port wired to a live inbound session
- **THEN** the port stays registered while the session is live

### Requirement: Auto-export enumerates local ports
At start, the listener SHALL enumerate the local sequencer ports and SHALL create an export for each port matching the `alsa_hw_auto_export` settings: the type filter (hardware/software/system), the positive and negative name regexes, and the exclusion of the daemon's own client ports ("no own ports"). Each exported port SHALL be registered as a per-device export on the rtpmidi server (own listen socket pair and announcement). Exporting a local port SHALL NOT create a daemon-owned seq port at export time; on connection the listener SHALL create a per-connection subscription peer: a daemon port subscribed both ways to the existing local port, registered as a hosted peer for the duration of the connection and removed with it.

#### Scenario: Matching ports exported
- **WHEN** the daemon starts with `alsa_hw_auto_export` enabled
- **THEN** every local port matching the type filter and name regexes is exported, excluding the daemon's own ports

#### Scenario: Own ports excluded
- **WHEN** enumeration finds the daemon's own client ports
- **THEN** they are never exported (no self-loop)

### Requirement: Auto-export tracks port changes
The listener SHALL subscribe to sequencer port add/remove announcements and SHALL create an export when a matching port appears and remove it when the port disappears. Exports SHALL reflect the current local port set.

#### Scenario: Port hotplug
- **WHEN** a matching local port is added or removed at runtime
- **THEN** the corresponding export is created or removed accordingly

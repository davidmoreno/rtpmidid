# On-demand Connections Specification

## Purpose

Lazy ALSA-triggered outbound rtpmidi sessions: `[connect_to]` sections and mdns-discovered servers expose waiting ALSA ports; the session is created only when an ALSA client subscribes and torn down on the last unsubscribe.

## Requirements

### Requirement: Waiting ports for outbound remotes
Every `[connect_to]` section and every mdns-discovered server SHALL create a waiting ALSA port named after the remote. While the port is waiting, no network session, socket, DNS resolution, or IN/CK handshake for that remote SHALL exist. The waiting port SHALL NOT be registered with the router.

#### Scenario: connect_to at startup
- **WHEN** the daemon starts with a `[connect_to]` section
- **THEN** an ALSA port named after the section is created and no network connection to the remote is attempted

#### Scenario: discovery creates a waiting port only
- **WHEN** an mdns server is discovered
- **THEN** a waiting ALSA port is created and no network client is spawned

### Requirement: Subscription initiates the session
The first ALSA subscription to a waiting port SHALL register the port as a hosted peer with the router and SHALL start the rtpmidi session to the remote: a network client peer SHALL be spawned (initiator mode, DNS on the worker) and connected to the port both ways, unless a session with the same remote already exists. When a session with the same remote already exists (inbound or outbound), the port SHALL be wired to the existing session and no duplicate client SHALL be spawned.

#### Scenario: Subscribe starts the session
- **WHEN** an ALSA client subscribes to a waiting port whose remote has no session
- **THEN** the port is registered as a hosted peer, a network client is spawned to the remote, and both are wired together

#### Scenario: Subscribe reuses an existing session
- **WHEN** an ALSA client subscribes to a waiting port whose remote already has an inbound session
- **THEN** the port is registered and wired to the existing session, and no new network client is spawned

### Requirement: Last unsubscribe tears down the session
The last ALSA unsubscribe from a waiting port SHALL unregister the port as a hosted peer. If the port's session is not shared with an inbound connection, the network client SHALL be removed (stopped and joined) and the session closed. If the session is shared with an inbound connection, the client SHALL NOT be removed; only the port SHALL be unregistered.

#### Scenario: Unsubscribe removes the client
- **WHEN** the last ALSA subscriber unsubscribes from a port whose session was initiated by that subscription
- **THEN** the port is unregistered and the network client peer is removed, closing the session

#### Scenario: Unsubscribe keeps a shared session
- **WHEN** the last ALSA subscriber unsubscribes from a port wired to an inbound session
- **THEN** only the port is unregistered and the inbound session remains

### Requirement: connect_to local UDP port binding
The `local_udp_port` setting of a `[connect_to]` section SHALL apply when the daemon is the initiator: the spawned network client SHALL bind that base port. When the session is served by an inbound connection (reuse), the setting SHALL have no effect.

#### Scenario: Initiator binds the configured port
- **WHEN** a `[connect_to]` section with `local_udp_port` starts a session as the initiator
- **THEN** the network client binds the configured local base port

#### Scenario: Reused session ignores the port
- **WHEN** a waiting port with a `local_udp_port` setting is wired to an existing inbound session
- **THEN** no bind uses the configured port

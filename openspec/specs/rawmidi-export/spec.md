# rawmidi Export Specification

## Purpose

Server-mode rawmidi devices are registered for export without being opened — the device (exclusive access) is opened only when a network connection actually needs it, and closed when the connection ends. Client-mode rawmidi keeps today's eager behavior.

## Requirements

### Requirement: Server-mode rawmidi is registered without opening
A server-mode `[rawmidi]` section (empty or absent `hostname`) SHALL register the device in the rtpmidi server's export registry as its name and device path (`device` setting), and SHALL create its listen socket pair (honoring `local_udp_port` when set) and announcement. The device SHALL NOT be opened while no connection needs it.

#### Scenario: Device not opened at startup
- **WHEN** the daemon starts with a server-mode `[rawmidi]` section
- **THEN** the device is registered and announced but its file descriptor is not opened

#### Scenario: No open while idle
- **WHEN** no network connection to the rawmidi export exists
- **THEN** the device remains closed and available to other programs

### Requirement: Device opened on connection
An inbound connection to a rawmidi export SHALL open the device (`O_RDWR | O_NONBLOCK`), spawn the rawmidi peer owning the file descriptor, and wire it to the acceptor peer. If the device cannot be opened, the connection SHALL be removed and the failure SHALL be logged; the export SHALL remain registered for future connections.

#### Scenario: Connection opens the device
- **WHEN** a connection arrives on a rawmidi export's port
- **THEN** the device is opened, the rawmidi peer is spawned, and both peers are wired

#### Scenario: Open failure rejects the connection
- **WHEN** a connection arrives but the device is busy or cannot be opened
- **THEN** the connection is removed with an error log and the export stays registered

### Requirement: Device closed on disconnect
When the session for a rawmidi export ends (remote closed or removed), the rawmidi peer SHALL be removed, which SHALL close the device file descriptor and release exclusive access.

#### Scenario: Disconnect closes the device
- **WHEN** the network session of a rawmidi export ends
- **THEN** the rawmidi peer is removed and the device file descriptor is closed

### Requirement: Client-mode rawmidi stays eager
A client-mode `[rawmidi]` section (`hostname` set) SHALL behave as today: the device SHALL be opened and the daemon SHALL connect out to `hostname:remote_udp_port` at startup, holding the session for the daemon's lifetime.

#### Scenario: Client mode connects at startup
- **WHEN** the daemon starts with a client-mode `[rawmidi]` section
- **THEN** the device is opened and an outbound session to the remote server is established immediately

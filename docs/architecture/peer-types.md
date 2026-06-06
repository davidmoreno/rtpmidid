# Peer types reference

All MIDI endpoints are `midipeer_t` instances. This table maps C++ classes to
wire strings, identity prefixes, and factory rules.

Implementation: [`src/peer_kind.cpp`](../../src/peer_kind.cpp),
[`src/peer_factory.cpp`](../../src/peer_factory.cpp).

## Summary table

| Class | `get_type()` | `peer_kind_e` | Identity prefix | Creatable from string? |
|-------|--------------|---------------|-----------------|------------------------|
| `peer_device_alsa_seq_t` | `peer_device_alsa_seq_t` | `device_alsa_seq` | `alsa_seq` | Yes |
| `peer_device_rawmidi_t` | `peer_device_rawmidi_t` | `device_rawmidi` | `rawmidi` | Yes |
| `peer_device_rtpmidi_client_t` | `peer_device_rtpmidi_client_t` | `device_rtpmidi_client` | `rtpmidi_client` | Yes |
| `peer_device_rtpmidi_session_t` | `peer_device_rtpmidi_session_t` | `device_rtpmidi_session` | `rtpmidi_session` | Attachment only |
| `peer_export_alsa_network_t` | `peer_export_alsa_network_t` | `export_alsa_network` | `alsa_multi` | Yes |
| `peer_export_rtpmidi_server_t` | `peer_export_rtpmidi_server_t` | `export_rtpmidi_server` | `rtpmidi_server` | Yes |
| `peer_import_rtpmidi_t` | `peer_import_rtpmidi_t` | `import_rtpmidi` | `rtpmidi_multi` | Yes |
| `peer_import_alsa_rtp_t` | `peer_import_alsa_rtp_t` | `import_alsa_rtp` | `alsa_listener` | Yes |
| `webui_midi_monitor_peer_t` | `webui_midi_monitor_peer_t` | `webui_monitor` | *(none)* | `monitor.start` only |

## Device peers

### `alsa_seq` → `peer_device_alsa_seq_t`

Simple ALSA sequencer port for MIDI routing.

| Field | Required | Notes |
|-------|----------|-------|
| `client` | If port given | ALSA client number |
| `port` | If client given | ALSA port number |
| `name` | No | Display only; without client/port creates port with `sub_client/sub_port = -1` |

### `rawmidi` → `peer_device_rawmidi_t`

Raw MIDI device (`/dev/snd/midiC*D*`).

| Field | Required | Notes |
|-------|----------|-------|
| `device` | Yes | Device path |
| `name` | No | Defaults to `device` |

### `rtpmidi_client` → `peer_device_rtpmidi_client_t`

RTP client connecting to a remote server.

| Field | Required | Notes |
|-------|----------|-------|
| `hostname` | Yes* | Remote host |
| `service` | Yes* | mDNS service name |
| `port` | No | Default `5004` |

\* Or provide `rtpclient` attachment via `create_peer()` (spawn path).

### `rtpmidi_session` → `peer_device_rtpmidi_session_t`

Active server-side RTP session. **Not creatable from bare identity** — requires
`rtppeer` attachment when a remote client connects.

| Field | Notes |
|-------|-------|
| `hostname`+`service` or `name` | Serialization only |

## Export peers

### `alsa_multi` → `peer_export_alsa_network_t`

ALSA "Network Export" port. Each ALSA subscription spawns an
`rtpmidi_server` child.

| Field | Required |
|-------|----------|
| `name` | Yes |

### `rtpmidi_server` → `peer_export_rtpmidi_server_t`

Single RTP server endpoint, announced via mDNS.

| Field | Required | Notes |
|-------|----------|-------|
| `name` | Yes | Service name |
| `port` | No | UDP port |

## Import peers

### `rtpmidi_multi` → `peer_import_rtpmidi_t`

RTP multi-listener. Each incoming connection spawns `peer_device_alsa_seq_t` +
`peer_device_rtpmidi_session_t` and wires them.

| Field | Required |
|-------|----------|
| `name` | Yes |
| `port` | Yes |

Does not send/receive MIDI through the router itself.

### `alsa_listener` → `peer_import_alsa_rtp_t`

ALSA port that connects to a remote RTP server when an ALSA subscription is made
(lazy connect).

| Field | Required | Notes |
|-------|----------|-------|
| `service` or `name` | One of | Remote service |
| `hostname` | No | Default from discovery |
| `port` | No | Default `5004` |
| `local_udp_port` | No | Default `0` |

May return an existing peer if `remote_name` matches.

## Internal

### `webui_monitor` → `webui_midi_monitor_peer_t`

Web UI MIDI monitor sink. Created by `monitor.start` with `target_peer_id` and
`monitor_uuid`. Never persisted. Duplicates router edges into the monitor.

## Factory API

```cpp
peer_factory_context_t ctx{aseq, router, mdns};
create_peer_from_string("rawmidi:device=/dev/snd/midiC0D0", ctx, &err);
create_peer({identity, attachment, rtppeer, rtpclient, ...}, ctx, &err);
```

Spawn helpers: [`src/peer_spawn.cpp`](../../src/peer_spawn.cpp) —
`spawn_import_rtpmidi_connection`, `spawn_alsa_network_server`,
`spawn_alsa_listener_client`, `ensure_peer_for_identity`, `apply_ini_connects`.

Identity from live peers: [`src/device_identity_from_peer.cpp`](../../src/device_identity_from_peer.cpp).

## Related docs

- [entities.md](entities.md) — device vs peer vs connection
- [discovery.md](discovery.md) — automatic peer creation
- [overview.md](overview.md) — conceptual overview

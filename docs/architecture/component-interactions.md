# Component interactions

Sequence diagrams for startup, MIDI routing, and discovery.

## Startup

```mermaid
sequenceDiagram
    participant Main as main()
    participant Settings as settings_t
    participant Router as midirouter_t
    participant ASEQ as aseq_t
    participant mDNS as mdns_rtpmidi_t
    participant Control as control_socket_t

    Main->>Settings: parse_argv()
    Main->>ASEQ: Create with alsa_name
    Main->>mDNS: Create
    Main->>Router: Create
    Main->>Control: Create and attach router/aseq/mdns
    Main->>Main: setup peers from INI
    Main->>Main: apply_ini_connects()
    Main->>Main: Enter poller.wait() loop
```

INI peers created via `create_peer_from_string()`; connects via
`apply_ini_connects()` in [`src/peer_spawn.cpp`](../../src/peer_spawn.cpp).

## MIDI data flow (network → ALSA)

```mermaid
sequenceDiagram
    participant UDP as UDP Socket
    participant Poller as poller_t
    participant NetPeer as peer_device_rtpmidi_session_t
    participant Router as midirouter_t
    participant ALSAPeer as peer_device_alsa_seq_t
    participant ASEQ as aseq_t

    UDP->>Poller: FD readable
    Poller->>NetPeer: callback(fd)
    NetPeer->>NetPeer: Parse RTP-MIDI packet
    NetPeer->>Router: send_midi(peer_id, mididata)
    Router->>Router: Lookup connected peers
    Router->>ALSAPeer: send_midi(from_id, mididata)
    ALSAPeer->>ASEQ: Write MIDI event
```

Encoding details: [midi-data-path.md](midi-data-path.md).

## mDNS discovery → connection

```mermaid
sequenceDiagram
    participant mDNS as mdns_rtpmidi_t
    participant Handler as rtpmidi_remote_handler_t
    participant Router as midirouter_t
    participant Listener as peer_import_alsa_rtp_t
    participant ASEQ as aseq_t

    mDNS->>Handler: Remote service discovered
    Handler->>Handler: Check name regex filters
    Handler->>Router: ensure_peer_for_identity
    Router->>Listener: Assign peer_id
    Listener->>ASEQ: Create ALSA port
    Note over Listener: Waits for ALSA subscription
    ASEQ->>Listener: Connection event
    Listener->>Listener: Create RTP client, connect
```

Lazy RTP connect: only when ALSA port is subscribed. See [discovery.md](discovery.md).

## Control plane read (status)

```mermaid
sequenceDiagram
    participant Client as control/WebSocket thread
    participant Router as midirouter_t
    participant Peers as peer threads

    Client->>Router: status_rows() enqueue LOW query
    Router->>Peers: request_internal_latency_stats (parallel)
    Peers-->>Router: reply_channel replies
    Router-->>Client: router_peer_row_t vector
    Client->>Client: dm-json serialize
```

Status is control-plane only — not on the MIDI HIGH path.

## Related docs

- [components.md](components.md) — type reference
- [event-loop.md](event-loop.md) — threads and shutdown
- [concurrency.md](concurrency.md) — queue dispatch

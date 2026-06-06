# Documentation index

## For users

| Doc | Description |
|-----|-------------|
| [user/configuration.md](user/configuration.md) | INI setup, defaults, ALSA usage |
| [user/web-ui.md](user/web-ui.md) | Browser UI — devices, connections, monitor |
| [user/control.md](user/control.md) | CLI and control socket (optional) |
| [user/troubleshooting.md](user/troubleshooting.md) | Common problems |
| [user/docker.md](user/docker.md) | Docker / Compose |

## Architecture

| Doc | Description |
|-----|-------------|
| [architecture/overview.md](architecture/overview.md) | Peer-router pattern |
| [architecture/entities.md](architecture/entities.md) | Devices, peers, connections, identities |
| [architecture/peer-types.md](architecture/peer-types.md) | Peer class reference |
| [architecture/database.md](architecture/database.md) | SQLite persistence |
| [architecture/components.md](architecture/components.md) | midirouter, midipeer, services |
| [architecture/component-interactions.md](architecture/component-interactions.md) | Sequence diagrams |
| [architecture/event-loop.md](architecture/event-loop.md) | Threads, poller, shutdown |
| [architecture/concurrency.md](architecture/concurrency.md) | Actor queues |
| [architecture/discovery.md](architecture/discovery.md) | mDNS → peers |
| [architecture/midi-data-path.md](architecture/midi-data-path.md) | MIDI encoding |
| [architecture/rtp-midi-networking.md](architecture/rtp-midi-networking.md) | librtpmidid RTP layer |
| [architecture/performance.md](architecture/performance.md) | Hot-path rules |

## Development

| Doc | Description |
|-----|-------------|
| [development/configuration.md](development/configuration.md) | Full INI reference |
| [development/control-protocol.md](development/control-protocol.md) | JSON-RPC API |
| [development/dm-json.md](development/dm-json.md) | JSON code generation |
| [development/frontend.md](development/frontend.md) | Web UI source layout |
| [development/logging.md](development/logging.md) | Log macros and levels |
| [development/testing.md](development/testing.md) | Test suite, TDD |
| [development/development-notes.md](development/development-notes.md) | Limits, extension recipes |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | Build workflow |

## Reference

| Doc | Description |
|-----|-------------|
| [reference/rfc6295-notes.md](reference/rfc6295-notes.md) | RTP-MIDI packet format |
| [../lib/STATEMACHINES.md](../lib/STATEMACHINES.md) | rtpclient state machine |
| [../README.librtpmidid.md](../README.librtpmidid.md) | librtpmidid intro |
| [reference/historical/plan-devices-peers-connections-evolution.md](reference/historical/plan-devices-peers-connections-evolution.md) | Historical design plan |

## Entry points

- **Users:** [../README.md](../README.md) → [user/](user/)
- **Developers / agents:** [../AGENTS.md](../AGENTS.md)

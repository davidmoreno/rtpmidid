# Midipeer typed interface Specification

## Purpose

Typed peer status and command interfaces between router, peers, and control socket, with no dynamic JSON value type in the internal model.

## Requirements

### Requirement: Typed status interface preserving the wire shape
`midipeer_t::status()` SHALL return a `std::variant` of per-peer-type status structs instead of a dynamic JSON value. Each alternative SHALL be a flat entry: the router-assigned common members (`id`, `send_to`, `stats`, `type`) plus that peer type's exact current fields with their current key names (e.g. RTP's `name` and `peer` subtree; ALSA's `name` and `port`; rawmidi's `name`, `device`, `status`). Serializing the variant SHALL produce the same wire object shape as today. There SHALL be no dynamic JSON value type anywhere in the internal model.

#### Scenario: Status returns typed variant
- **WHEN** code calls `peer->status()` on any peer
- **THEN** it receives a `std::variant` of typed per-type status structs, never a JSON value type

#### Scenario: RTP wire shape preserved
- **WHEN** an RTP peer's status variant is serialized
- **THEN** the output object contains `name` and a `peer` key holding the latency/local/remote subtrees, exactly as today

#### Scenario: ALSA wire shape preserved
- **WHEN** an ALSA peer's status variant is serialized
- **THEN** the output object contains `name` and `port` at the top level, exactly as today

#### Scenario: Router sets common members generically
- **WHEN** the router enriches a peer status
- **THEN** it assigns `id`, `send_to`, `stats`, and `type` via a generic visit over the variant, and every alternative declares those members (a new peer type omitting them fails to compile)

### Requirement: Per-type status structs
Each peer type SHALL define a `/// [JSON-DM]`-decorated status struct holding the router-assigned common members and that type's own fields, replacing the ad-hoc payloads built today (e.g. `peer_status()` in `utils.cpp` becomes typed latency/local/remote structs).

#### Scenario: RTP status struct
- **WHEN** the RTP status struct is defined
- **THEN** it contains `id`, `send_to`, `stats`, `type`, `name`, and the `peer` subtree structs (latency_ms, status, local, remote)

#### Scenario: Rawmidi status struct
- **WHEN** the rawmidi status struct is defined
- **THEN** it contains `id`, `send_to`, `stats`, `type`, `name`, `device`, and `status`

### Requirement: No dynamic JSON in the internal model
The internal model SHALL NOT contain a dynamic JSON value type. JSON SHALL appear only as: (a) the wire format on the control socket, and (b) the raw `params_json` string passed across the peer `command()` interface boundary, parsed immediately into typed structs inside the implementation.

#### Scenario: Codebase has no JSON value type
- **WHEN** the codebase is scanned for dynamic JSON value types
- **THEN** none are found; all payloads are typed structs or scalar/container members

### Requirement: Peer command boundary
`midipeer_t::command(cmd, data)` SHALL accept the command name and the params as a raw JSON string (`std::string_view`), replacing the JSON value parameter. Each implementation SHALL parse the params string into its own typed command structs and SHALL return its result serialized as a JSON string. The wire bytes received and returned by peer commands SHALL be unchanged. The generic dispatch over command names SHALL remain on the interface.

#### Scenario: Command with typed params
- **WHEN** a peer command such as `status` is invoked on a peer
- **THEN** the implementation parses the raw params into its typed structs and returns the serialized typed result with the same wire shape as today

#### Scenario: Unknown command error
- **WHEN** a peer receives a command name it does not implement
- **THEN** it returns an error result identifying the command as not implemented

### Requirement: Router aggregation is typed
`midirouter_t::status()` SHALL aggregate peer statuses as `std::vector<std::variant<...>>` (one variant entry per peer), enriching each with router-assigned members via generic visit, without constructing any ad-hoc JSON. The serialized `router` array SHALL be byte-compatible with today's.

#### Scenario: Router status list
- **WHEN** the router status is built
- **THEN** it is a `std::vector` of status variants with one entry per peer, each carrying `id`, `send_to`, `stats`, `type`, and the peer's own fields

# Midipeer typed interface — delta for add-actor-architecture

## MODIFIED Requirements

### Requirement: Peer command boundary
`midipeer_t::command(cmd, data)` SHALL accept the command name and the params as a raw JSON string (`std::string_view`), replacing the JSON value parameter. Each implementation SHALL parse the params string into its own typed command structs and SHALL return its result serialized as a JSON string. The wire bytes received and returned by peer commands SHALL be unchanged. The generic dispatch over command names SHALL remain on the interface. Commands SHALL be delivered to the peer as control-lane mailbox messages and SHALL execute on the peer's own actor thread; the result SHALL be returned as a response message carrying the request's correlation id. No other actor SHALL invoke peer command logic synchronously across threads.

#### Scenario: Command with typed params
- **WHEN** a peer command such as `status` is invoked on a peer
- **THEN** the implementation parses the raw params into its typed structs and returns the serialized typed result with the same wire shape as today

#### Scenario: Unknown command error
- **WHEN** a peer receives a command name it does not implement
- **THEN** it returns an error result identifying the command as not implemented

#### Scenario: Command executes on the peer thread
- **WHEN** the router relays a command to a peer
- **THEN** the command handler runs inside the peer actor's loop, reading only the peer's thread-local state, and posts the response message back

### Requirement: Router aggregation is typed
Status aggregation SHALL be typed: the requester issuing the status command SHALL merge peer statuses as `std::vector<std::variant<...>>` (one variant entry per peer), enriching each with router-assigned members (`id`, `send_to`, `stats`, `type`) via generic visit, without constructing any ad-hoc JSON. The serialized `router` array SHALL be byte-compatible with today's. Aggregation SHALL be asynchronous and requester-driven: the router answers the requester with a status head carrying router-side statistics and the router-assigned members of each expected peer and scatters the status request with the requester's `reply_to`; each peer produces its typed status variant from its own thread-local state and replies directly to the requester; the requester merges the responses under its own deadline (partial results permitted, unresponsive peers marked). The router SHALL keep no gather state.

#### Scenario: Router status list
- **WHEN** the router status is built
- **THEN** it is a `std::vector` of status variants with one entry per peer, each carrying `id`, `send_to`, `stats`, `type`, and the peer's own fields

#### Scenario: Peer status produced on its own thread
- **WHEN** a peer receives a status request message
- **THEN** it builds its typed status variant from thread-local state and replies directly to the requester's mailbox; the router never reads peer state directly and keeps no gather state

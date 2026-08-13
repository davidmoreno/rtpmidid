# Control socket jsondm — delta for add-actor-architecture

## ADDED Requirements

### Requirement: Per-connection actors with selective waits
A control listener actor SHALL own the listening socket and SHALL spawn one connection actor per accepted client, owning that client's fd and mailbox at normal priority. Commands that require actor-system work (status, connect/disconnect, peer add/remove, peer commands) SHALL be posted as typed requests with a correlation id and a `reply_to` mailbox handle; the connection actor SHALL wait for the response with a selective wait under a requester-side deadline and write it to the client itself. The wire protocol shapes SHALL be unchanged; only the execution becomes asynchronous.

#### Scenario: Response matched to client
- **WHEN** a client issues a command and the actor system responds
- **THEN** the connection actor for that client writes the response to it, with the echoed request id

#### Scenario: Multiple concurrent clients
- **WHEN** two clients have requests in flight simultaneously
- **THEN** each connection actor waits on and receives its own responses independently

#### Scenario: Stalled client does not block others
- **WHEN** one client's request is slow or its connection is unresponsive
- **THEN** other connection actors and the data plane continue unaffected

### Requirement: Request deadlines produce error responses
Each selective wait SHALL have a requester-side deadline. If the router or a peer does not answer in time, the connection actor SHALL compose and send an error response identifying the unresponsive component, in the protocol's error form.

#### Scenario: Unresponsive router
- **WHEN** a pending request deadline expires without a response
- **THEN** the client receives an error response instead of waiting indefinitely

### Requirement: Client disconnect abandons waits
When a client connection closes, its connection actor SHALL stop and its in-flight waits SHALL be abandoned. Responses arriving afterwards for those correlation ids SHALL be silently discarded.

#### Scenario: Disconnect mid-gather
- **WHEN** a client disconnects while its status request is being gathered and responses arrive later
- **THEN** the responses are discarded without error and no write is attempted to the closed connection

## ADDED Requirements

### Requirement: Typed command registry
The control socket command registry SHALL be typed: each entry SHALL declare a params struct type and a result struct type (`command_t<ParamsT, ResultT>`), with a handler receiving `const ParamsT&` and returning `ResultT`. No command handler SHALL receive or return a dynamic JSON value.

#### Scenario: Typed handler invocation
- **WHEN** a command is dispatched
- **THEN** its params are deserialized into the command's `ParamsT` and the handler returns a `ResultT` value

#### Scenario: No dynamic values in handlers
- **WHEN** command handlers are reviewed
- **THEN** none of them construct or consume dynamic JSON value objects

### Requirement: Request dispatch without a dynamic value
Request dispatch SHALL read the `method` field via a Reader pre-scan (save/restore position over the raw request bytes), select the typed registry entry, then deserialize the per-command request struct `{id, params}` — with the `method` key skipped as an unknown key. The request `id` SHALL be `std::optional<std::string>` because requests may omit it (the current daemon echoes `"id": null`).

#### Scenario: Method pre-scan dispatch
- **WHEN** a request `{"method": "router.remove", "params": [..]}` (with or without `id`) arrives
- **THEN** the method selects its typed entry and the request struct parses `id` (if present) and `params` from the same bytes

#### Scenario: Unknown method error
- **WHEN** a request method matches no registry entry and no peer command pattern
- **THEN** an error response identifying the unknown method is returned

### Requirement: Typed response composition
Responses SHALL be composed from typed parts directly in the Writer — `{"id": <string or null>, "result": <ResultT>}` or `{"id": <string or null>, "error": <string>}` — without an envelope struct and without dynamic values. Errors raised as exceptions during dispatch or serialization SHALL be caught at the boundary and serialized as the error form.

#### Scenario: Success response composition
- **WHEN** a handler returns a `ResultT` value
- **THEN** the response object contains the echoed id (or `null` when absent from the request) and the serialized result

#### Scenario: Exception becomes error response
- **WHEN** dispatch or serialization throws
- **THEN** the response object contains the echoed id and the exception message as `error`

### Requirement: Direct fd streaming with success terminator
Responses SHALL be serialized directly to the client socket file descriptor. The protocol's trailing newline SHALL be written only after the response serializes successfully, so a mid-serialization error leaves an unterminated line.

#### Scenario: Successful response is newline-terminated
- **WHEN** a response serializes without error
- **THEN** the bytes sent to the client end with a newline

#### Scenario: Failed response is unterminated
- **WHEN** serialization throws mid-write
- **THEN** no trailing newline is written and the partial payload is left on the socket

### Requirement: Wire protocol preserved
The wire protocol SHALL remain byte-compatible: request/response envelopes, per-command params forms (arrays and objects), result shapes (including bare-string results like `"ok"`), and peer status shapes SHALL be unchanged. Command params that are JSON arrays (e.g. `router.remove` → `[id]`) SHALL use positional-array-mode structs; commands with multiple legacy params forms (e.g. `connect`) SHALL use a hand-written params reader built on the public Reader API at the boundary. The CLI and `docs/CONTROL.md` SHALL NOT require protocol changes.

#### Scenario: Array params preserved
- **WHEN** the `router.remove` command is invoked with params `[id]`
- **THEN** the array is deserialized into the command's positional-array params struct and the command executes

#### Scenario: Legacy multi-form params preserved
- **WHEN** the `connect` command is invoked with any of its four accepted params forms
- **THEN** all forms are accepted and handled as today

#### Scenario: Result shapes preserved
- **WHEN** a command that returns `"ok"` today is invoked
- **THEN** the response result is the string `"ok"` as today

#### Scenario: Byte-compat verified by tests
- **WHEN** the migration is complete
- **THEN** byte-compatibility tests comparing new serialization against captured legacy payloads pass

### Requirement: Command set and framing preserved
The command names, descriptions, and the newline-delimited framing SHALL be preserved.

#### Scenario: Commands still dispatch by name
- **WHEN** a client issues any command from the command set
- **THEN** the command dispatches by its name with unchanged semantics

# slow-message-detection Specification

## Purpose
TBD - created by archiving change slow-message-detection. Update Purpose after archive.
## Requirements
### Requirement: Per-actor slow-message threshold

Each actor SHALL have a configurable slow-message threshold (`slow_message_threshold`), defaulting to 1000 milliseconds, used to decide whether a message handler is slow. The threshold MUST be settable per actor before the actor starts or first pumps.

#### Scenario: Default threshold

- **WHEN** an actor is constructed without overriding `slow_message_threshold`
- **THEN** its threshold is 1000 milliseconds

#### Scenario: Per-actor override

- **WHEN** an owner sets `slow_message_threshold` to a custom value (e.g. `0ms`)
- **THEN** that value is used for that actor only

### Requirement: Message handler duration is measured

The actor SHALL measure the wall-clock duration of each `on_data` and `on_control` invocation with a monotonic clock. The measurement SHALL cover only the handler invocation, not queue draining, poller waits, or the `stop_t` message. A handler that throws SHALL NOT be slow-checked.

#### Scenario: Data handler timed

- **WHEN** a data-lane message is handled
- **THEN** the duration of its `on_data` invocation is measured with a monotonic clock

#### Scenario: Control handler timed

- **WHEN** a control-lane message other than `stop_t` is handled
- **THEN** the duration of its `on_control` invocation is measured with a monotonic clock

#### Scenario: Stop message exempt

- **WHEN** a `stop_t` control message is handled
- **THEN** no duration is measured for it and no slow-message ERROR can be emitted for it

#### Scenario: Throwing handler not slow-checked

- **WHEN** a handler throws and the existing exception isolation logs it
- **THEN** no slow-message ERROR is emitted for that invocation

### Requirement: Slow message emits an ERROR

When a message handler's measured duration exceeds the actor's threshold, the actor SHALL log an ERROR identifying the actor name, the lane (data or control), the message (type name, and content where a rendering is available), and the measured duration. The ERROR SHALL NOT be rate-limited.

#### Scenario: Slow data message logged

- **WHEN** a data-lane handler takes longer than the threshold
- **THEN** an ERROR is logged naming the actor, the data lane, the message, and the duration

#### Scenario: Slow control message logged

- **WHEN** a control-lane handler takes longer than the threshold
- **THEN** an ERROR is logged naming the actor, the control lane, the message, and the duration

#### Scenario: Fast message not logged

- **WHEN** a handler completes within the threshold
- **THEN** no slow-message ERROR is emitted for it

#### Scenario: Message identified by type when no rendering exists

- **WHEN** a slow message's type has no `to_string` overload
- **THEN** the ERROR names the message by its demangled type name

### Requirement: Deterministic testability

Slow-message detection SHALL be testable in threadless pump mode: a test SHALL be able to set a zero threshold, post one message, pump once, and observe the slow-message ERROR deterministically, without sleeping.

#### Scenario: Zero-threshold pump triggers the error

- **WHEN** a test sets `slow_message_threshold` to `0ms`, posts a message, and pumps
- **THEN** the slow-message ERROR is emitted for that message


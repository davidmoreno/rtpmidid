# logfmt Structured Logging Specification

## Purpose

Structured, machine-parseable log output: producer call sites emit logfmt-style `key=value` pairs (with a `quoted_t` helper for values needing quoting), the renderer prepends the built-in `level=`, `thread=`, and `filename=` fields and aligns the message body, and keys are color-highlighted (level by severity, built-in keys blue, producer keys yellow) under an environment/CLI/INI/TTY-controlled color flag.

## Requirements

### Requirement: Key-value log message formatting

The logging library SHALL support emitting log messages as a human-readable message (bare text) followed by logfmt `key=value` pairs separated by single spaces. Call sites SHALL write keys literally in the format string and SHALL wrap string values that may require quoting in the `quoted_t` helper.

#### Scenario: Key-value message renders verbatim

- **WHEN** a call site logs `INFO("value={}", quoted_t{std::string("x")})`
- **THEN** the message body renders as `value=x`

#### Scenario: Prose with a key-value pair

- **WHEN** a call site logs `INFO("Failed to open file filename={}", quoted_t{filename})`
- **THEN** the message body renders as `Failed to open file filename=<filename>`

#### Scenario: Multiple pairs are space-separated

- **WHEN** a call site logs `INFO("connection={} from={}", quoted_t{a}, quoted_t{b})`
- **THEN** the message body renders as `connection=<a> from=<b>` with a single space between pairs

### Requirement: quoted_t value quoting and escaping

The `quoted_t` helper SHALL render a value unquoted when it contains no space, `=`, or `"`; SHALL wrap the value in double quotes when it contains any of those characters; SHALL escape `\` as `\\` and `"` as `\"` inside quotes; SHALL escape control characters newline and tab as the literal sequences `\n` and `\t`; and SHALL render an empty value as an empty unquoted value (`key=`).

#### Scenario: Safe value is not quoted

- **WHEN** a value is `router`
- **THEN** it renders as `router` with no quotes

#### Scenario: Value with spaces is quoted

- **WHEN** a value is `hello world`
- **THEN** it renders as `"hello world"`

#### Scenario: Embedded quote is escaped

- **WHEN** a value is `he said "hi"`
- **THEN** it renders as `"he said \"hi\""`

#### Scenario: Empty value renders bare

- **WHEN** a value is empty
- **THEN** it renders as an empty value (e.g. `thread=`), not `""`

### Requirement: Built-in log fields

The renderer SHALL prepend the fields `level=`, `thread=`, and `filename=` to every rendered line, in that order. The level SHALL be lowercase (`debug`, `info`, `warning`, `error`); the thread SHALL be the captured tag (empty when untagged); and the filename SHALL be the source basename and line number joined as `<basename>:<lineno>`. The built-in prefix SHALL be padded so the message body starts at a fixed column; longer prefixes SHALL overflow without truncation.

#### Scenario: Full line with tag

- **WHEN** a message has level INFO, tag `router`, file `router_actor.cpp`, lineno 12, body `peer up`
- **THEN** the rendered line is `level=info thread=router filename=router_actor.cpp:12 peer up`

#### Scenario: Untagged line has empty thread

- **WHEN** a message has an empty tag
- **THEN** the rendered line includes `thread=` as an empty value

### Requirement: Color highlighting of keys

When color is enabled, the renderer SHALL color the `level=<value>` token by severity (DEBUG blue, INFO uncolored, WARNING yellow, ERROR red), SHALL color the built-in keys `thread` and `filename` blue, and SHALL color every other key yellow. Values, bare prose, and separators SHALL remain in the default color.

#### Scenario: Built-in and custom keys differ in color

- **WHEN** a line `level=info thread=router name="peer up" connection=a` is rendered with color enabled
- **THEN** the `level=info` token is uncolored, the keys `thread` and `filename` are blue, and the keys `name` and `connection` are yellow

### Requirement: Quote-aware key detection for coloring

The colorizer SHALL treat an `=` inside a quoted value as part of the value, not as the start of a key.

#### Scenario: Equals inside a quoted value is not a key

- **WHEN** a line contains `name="a=b"` and color is enabled
- **THEN** only `name` is colored as a key; `a=b` is left in the default color

### Requirement: Color enable/disable configuration

The daemon SHALL compute a process-global color-enabled flag once at startup. The flag SHALL be off when `--log-no-color` is given; off when `NO_COLOR` is present and non-empty (taking precedence over any force); on when `FORCE_COLOR` is present and non-empty; otherwise governed by the INI `log_color` setting (`never`, `always`, or `auto`); and when `auto`, on if and only if standard output is a terminal. When the flag is off, the renderer SHALL NOT tokenize or colorize the line.

#### Scenario: NO_COLOR disables color

- **WHEN** the environment has `NO_COLOR` set to a non-empty value
- **THEN** color is disabled regardless of TTY or `FORCE_COLOR`

#### Scenario: FORCE_COLOR enables color

- **WHEN** `FORCE_COLOR` is set to a non-empty value and `NO_COLOR` is unset
- **THEN** color is enabled even when standard output is not a terminal

#### Scenario: Command-line flag disables color

- **WHEN** the daemon is started with `--log-no-color`
- **THEN** color is disabled

#### Scenario: Non-terminal default is no color

- **WHEN** no color option is set and standard output is not a terminal
- **THEN** color is disabled and the renderer emits the raw line without tokenizing

# Thread named logging Specification

## Purpose

Per-thread log context and OS-level thread naming: every log line identifies the producing actor thread via a `[tag]` in the rendered prefix, and actor/reaper threads appear as `rtpmidid:<name>` in htop/top -H/ps -eLf/gdb/perf, while the process comm stays `rtpmidid`.

## Requirements

### Requirement: Per-thread log tag capture

The logging library SHALL maintain a per-thread tag (a `thread_local` string settable by any thread) and SHALL capture the producing thread's tag into every emitted log message at production time, so the tag travels with the message regardless of which thread renders it. The captured value MUST be an owned copy, never a pointer into the producing thread's storage.

#### Scenario: Actor thread tags its own logs

- **WHEN** an actor thread sets its tag to the actor name and logs a message
- **THEN** the resulting `log_message_t` carries the actor name, independent of the thread that later renders it

#### Scenario: Tag survives producer thread exit

- **WHEN** a thread sets a tag, logs, and exits before the logger renders the message
- **THEN** the line still renders with the full tag (the message holds an owned copy, so no dangling reference)

#### Scenario: Thread without a tag

- **WHEN** a thread that never set a tag logs a message
- **THEN** the message carries an empty tag

### Requirement: Log line rendering with tag

The line renderer SHALL format a message as a logfmt line `level=<level> thread=<tag> filename=<basename>:<lineno> <body>`, where `<tag>` is the captured thread tag (empty when untagged) and `<body>` is the producer-formatted `key=value` message. Both the direct-print fallback and the logger actor SHALL use the same renderer, producing identical output.

#### Scenario: Tagged line format

- **WHEN** a message has level INFO, tag `router`, file `router_actor.cpp`, lineno 12, body `peer up`
- **THEN** the rendered line is `level=info thread=router filename=router_actor.cpp:12 peer up`

#### Scenario: Untagged line has empty thread field

- **WHEN** a message has an empty tag
- **THEN** the rendered line carries an empty `thread=` field (e.g. `level=info thread= filename=myfile.cpp:42 hello`)

### Requirement: Actor thread naming

Every actor thread SHALL set its log tag to the actor's configured name at thread start and SHALL set its OS thread name (comm) to `rtpmidid:<name>`. Comm names are subject to the 15-character limit; truncation SHALL be silent, and the full name SHALL remain in the log tag.

#### Scenario: Named actor thread

- **WHEN** an actor with name `router` starts its thread
- **THEN** the thread's log tag is `router` and its comm is `rtpmidid:router`

#### Scenario: Long actor name truncates in comm only

- **WHEN** an actor with name `control-conn-5` starts its thread
- **THEN** the comm is truncated to the 15-character limit, while the log tag keeps the full name `control-conn-5`

### Requirement: Background thread naming

The supervisor's reaper thread SHALL set its log tag to `reaper` and its comm to `rtpmidid:reaper` when it starts.

#### Scenario: Reaper thread starts

- **WHEN** the supervisor starts its reaper thread
- **THEN** the reaper's log tag is `reaper` and its comm is `rtpmidid:reaper`

### Requirement: Main thread tag without process rename

The main thread SHALL set its log tag to `main` before the first log call that must carry it, and SHALL NOT change its OS thread name, preserving the process comm `rtpmidid` for tools and scripts that match the process name.

#### Scenario: Early startup logs are tagged main

- **WHEN** the daemon logs during startup
- **THEN** the lines are tagged `[main]`

#### Scenario: Process name preserved

- **WHEN** the daemon is running
- **THEN** the process comm remains `rtpmidid` (unchanged by this feature)

### Requirement: Empty actor names produce no tag

An actor with an empty configured name SHALL produce an empty log tag (rendered as untagged) and SHALL NOT change its OS thread name — a bare `rtpmidid:` comm MUST never be set.

#### Scenario: Actor created with empty name

- **WHEN** an actor is created with an empty name and starts its thread
- **THEN** its log messages render untagged and its comm is left unchanged

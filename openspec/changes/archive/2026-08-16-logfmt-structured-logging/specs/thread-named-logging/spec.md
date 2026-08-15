## MODIFIED Requirements

### Requirement: Log line rendering with tag

The line renderer SHALL format a message as a logfmt line `level=<level> thread=<tag> filename=<basename>:<lineno> <body>`, where `<tag>` is the captured thread tag (empty when untagged) and `<body>` is the producer-formatted `key=value` message. Both the direct-print fallback and the logger actor SHALL use the same renderer, producing identical output.

#### Scenario: Tagged line format

- **WHEN** a message has level INFO, tag `router`, file `router_actor.cpp`, lineno 12, body `peer up`
- **THEN** the rendered line is `level=info thread=router filename=router_actor.cpp:12 peer up`

#### Scenario: Untagged line has empty thread field

- **WHEN** a message has an empty tag
- **THEN** the rendered line carries an empty `thread=` field (e.g. `level=info thread= filename=myfile.cpp:42 hello`)

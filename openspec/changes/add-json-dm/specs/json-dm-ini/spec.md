## ADDED Requirements

### Requirement: INI backend via dedicated generation
Structs decorated with `/// [INI-DM]` SHALL be generated with INI serializers/deserializers (a dedicated emitter alongside the JSON one; INI-DM structs SHALL NOT receive JSON code). A struct SHALL map to INI sections and `key = value` lines: scalar members of the root struct SHALL map to the `[general]` section; a nested struct member SHALL map to its own `[section]` named after the member; members may be of any type with a `jsondm::ini::to_value<T>`/`to_text<T>` conversion (config types such as `std::regex` and enums specialize the converters).

#### Scenario: Scalar member round-trip
- **WHEN** a struct with an `int` member `port` is serialized to INI and parsed back
- **THEN** the INI contains a `port = <value>` line and the member round-trips

#### Scenario: Nested struct becomes section
- **WHEN** a struct contains a nested decorated struct member `announce`
- **THEN** serialization emits a `[announce]` section containing the nested member's keys

### Requirement: Repeated sections from vector members
A `std::vector<T>` member SHALL serialize to repeated INI sections, one per element. Deserialization SHALL collect all repeated sections of the same name into the vector in file order.

#### Scenario: Vector emits repeated sections
- **WHEN** a struct has a `std::vector<announce_t>` member with two elements
- **THEN** serialization emits two sections of the same name, in element order

#### Scenario: Repeated sections parse into vector
- **WHEN** an INI file contains the same section name twice and is parsed into a struct with a matching vector member
- **THEN** the vector contains both elements in file order

### Requirement: Optional sections from optional members
A `std::optional<T>` member SHALL serialize to a section only when the optional is engaged; an empty optional SHALL omit the section. Deserialization of a missing section SHALL leave the optional empty.

#### Scenario: Empty optional omits section
- **WHEN** a struct with an empty `std::optional<announce_t>` member is serialized to INI
- **THEN** no section is emitted for that member

#### Scenario: Present optional emits section
- **WHEN** a struct with an engaged `std::optional<announce_t>` member is serialized to INI
- **THEN** the corresponding section is emitted

### Requirement: Existing configuration compatibility
The generated INI reader SHALL parse configuration files accepted by the former hand-written parser with equivalent resulting values, including tolerance for repeated sections, whitespace, comments, and the `{{hostname}}` placeholder (filled on the raw text before parsing). The generated writer SHALL emit files loadable by an INI parser. The hand-written parser SHALL be removed only after the generated reader passes the compatibility tests (verified on `default.ini` and the settings test suite).

#### Scenario: Parses current config semantics
- **WHEN** an INI file that the hand-written parser accepts is parsed by the generated reader
- **THEN** the resulting struct values match those produced by the hand-written parser

#### Scenario: Generated output readable by hand parser
- **WHEN** a struct is serialized to INI by the generated writer
- **THEN** the hand-written parser can load the output with equivalent values

### Requirement: INI error reporting
The INI reader SHALL report parse errors with file name and line number context, consistent with `rtpmidid::ini_exception`.

#### Scenario: Error includes line number
- **WHEN** parsing an INI file with a malformed line
- **THEN** the exception message includes the file name and line number of the error

### Requirement: Coexistence with the hand-written parser
The hand-written INI parser SHALL remain the active parser until the generated reader passes the compatibility requirements; the switchover SHALL be explicit and gated on test results.

#### Scenario: Hand parser still active
- **WHEN** the generated reader does not yet pass all compatibility scenarios
- **THEN** the daemon continues to use the hand-written parser

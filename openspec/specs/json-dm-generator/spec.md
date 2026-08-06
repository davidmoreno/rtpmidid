# Json dm generator Specification

## Purpose

Code generator (scripts/json_dm_to_cpp.py) that emits typed serializer/deserializer pairs from /// [JSON-DM] decorated C++ structs, failing loudly on unsupported member types.

## Requirements

### Requirement: Marker-based struct discovery
The generator SHALL scan input header files and include every struct immediately preceded by a `/// [JSON-DM]` comment line in the generated output. A struct preceded by `/// [JSON-DM-ARRAY]` SHALL be included with positional-array serialization (members mapped to array elements in declaration order). Any other `/// [JSON-DM...]` marker spelling SHALL fail loudly. Structs without a marker SHALL be ignored.

#### Scenario: Decorated struct is discovered
- **WHEN** the generator processes a header containing `/// [JSON-DM]` followed by a `struct Foo { ... };`
- **THEN** `Foo` is included in the generated serializer/deserializer output as an object-mode struct

#### Scenario: Array marker selects array mode
- **WHEN** the generator processes a header containing `/// [JSON-DM-ARRAY]` followed by a `struct Params { ... };`
- **THEN** `Params` is generated in positional-array mode

#### Scenario: Unknown marker fails loudly
- **WHEN** the generator encounters a `/// [JSON-DM...]` marker it does not recognize
- **THEN** generation fails with an error naming the struct

#### Scenario: Undecorated struct is ignored
- **WHEN** the generator processes a header containing a `struct Bar { ... };` without the marker
- **THEN** no serializer/deserializer is generated for `Bar`

### Requirement: Struct body parsing
The generator SHALL parse the struct body by balancing braces, correctly skipping nested braces inside default member initializers, comments, and string literals. It SHALL extract each member's type and name.

#### Scenario: Members with default initializers
- **WHEN** a decorated struct contains `int port{1234};` and `std::vector<int> items = {1, 2, 3};`
- **THEN** both members are extracted with type and name, ignoring the initializer expressions

#### Scenario: Nested template member types
- **WHEN** a member has type `std::unordered_map<std::string, std::vector<Foo>>`
- **THEN** the type is parsed correctly, treating commas inside angle brackets as part of the type

### Requirement: Supported member types
The generator SHALL support the following member types: integral types (`int`, `uint8_t`..`uint64_t`, `size_t`, etc.), `float`, `double`, `bool`, `std::string`, `std::vector<T>`, `std::unordered_map<std::string, T>`, `std::optional<T>`, `std::variant<Ts...>` (including `std::monostate`), and other JSON-DM-marked structs. Recursive types (`std::vector<Self>`, `std::unordered_map<std::string, Self>`) SHALL be supported.

#### Scenario: All supported types generate
- **WHEN** a decorated struct has members of every supported type
- **THEN** the generator emits valid serializer/deserializer code covering all members

#### Scenario: Recursive struct
- **WHEN** a decorated struct `Node` contains `std::vector<Node> children;`
- **THEN** the generator emits code for `Node` whose serializer recurses through `children`

### Requirement: Fail-loud validation
The generator SHALL terminate with a generation error naming the struct and member when a member type is not in the supported set. The generator SHALL NOT emit partial or silently wrong code for unsupported members.

#### Scenario: Unsupported member type
- **WHEN** a decorated struct contains `std::unique_ptr<int> ptr;`
- **THEN** generation fails with an error naming the struct and the offending member

#### Scenario: Unmarked nested struct used as member
- **WHEN** a decorated struct uses a type that is not a supported builtin and not itself JSON-DM-marked
- **THEN** generation fails with an error identifying the missing marker

### Requirement: Generated output layout
For each input header, the generator SHALL emit two files: `<name>_jsondm.hpp` containing the `jsondm::serializer<T>`/`jsondm::deserializer<T>` specialization declarations and `FMT::formatter<T>` adapter declarations, and `<name>_jsondm.cpp` containing the specialization definitions.

#### Scenario: Generated file pair
- **WHEN** the generator runs with `--input foo.hpp --header out/ --source out/`
- **THEN** it writes `out/foo_jsondm.hpp` and `out/foo_jsondm.cpp`

#### Scenario: Declarations usable across translation units
- **WHEN** a translation unit includes `foo_jsondm.hpp` and calls `jsondm::serialize(v, out)` with a generated struct
- **THEN** it compiles and links against the definitions in `foo_jsondm.cpp`

### Requirement: Deterministic output
The generator SHALL produce byte-identical output for the same input and options, with struct members emitted in declaration order.

#### Scenario: Repeatable generation
- **WHEN** the generator runs twice on the same input with the same options
- **THEN** both runs produce identical files

### Requirement: Command-line interface
The generator SHALL accept `--input <file>` (may be repeated), `--header <dir>`, and `--source <dir>` options, and SHALL report generated file paths on success.

#### Scenario: Multiple inputs
- **WHEN** the generator runs with two `--input` flags
- **THEN** each input produces its own generated file pair


### Requirement: Library usage from the runtime header
The generated header SHALL include the json-dm runtime header and the generated code SHALL reference runtime functions through the `jsondm` namespace.

#### Scenario: Generated code compiles against runtime
- **WHEN** the runtime header and generated files are compiled together
- **THEN** the generated serializer/deserializer definitions resolve all runtime symbols

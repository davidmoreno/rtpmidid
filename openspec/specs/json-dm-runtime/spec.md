# Json dm runtime Specification

## Purpose

Strict, allocation-friendly JSON (de)serialization runtime for decorated structs: streaming Writer/Reader, primitives, containers, optional/variant semantics, the fmt adapter, and the public serialize/deserialize API used by all generated code.

## Requirements

### Requirement: Public API
The runtime SHALL provide `jsondm::serialize(const T&, std::string&)` (appends JSON to the string), `jsondm::serialize(const T&, int fd)` (streams JSON to a file descriptor), and `jsondm::deserialize(std::string_view, T&)` (parses JSON from a zero-copy input view). These SHALL be the only entry points required by callers; the underlying `serializer<T>`/`deserializer<T>` specializations SHALL be generated per decorated struct.

#### Scenario: Serialize into a string
- **WHEN** `jsondm::serialize(foo, out)` is called with a decorated struct `foo` and a `std::string& out`
- **THEN** `out` receives the JSON representation of `foo` appended to any prior content

#### Scenario: Deserialize from a string view
- **WHEN** `jsondm::deserialize(R"({"id":1,"name":"x"})", foo)` is called with a matching decorated struct
- **THEN** `foo.id` is `1` and `foo.name` is `"x"`

#### Scenario: Serialize to a file descriptor
- **WHEN** `jsondm::serialize(foo, fd)` is called with `fd` open for writing
- **THEN** the JSON representation is written to `fd`, with no additional output after completion

### Requirement: Happy-path allocation freedom
The runtime SHALL perform no heap allocation on the serialization and deserialization happy paths for structs whose members are scalars, strings already holding sufficient capacity, and containers already holding sufficient capacity. Allocation is permitted only on error paths and when target containers must grow.

#### Scenario: Repeated serialize into reused string
- **WHEN** `jsondm::serialize(foo, out)` is called twice with the same `out` and no intervening `out` modifications
- **THEN** the second call performs no heap allocation

#### Scenario: Deserialize into reused struct
- **WHEN** `jsondm::deserialize(in, foo)` is called twice with the same `foo` whose strings and vectors retain capacity
- **THEN** the second call performs no heap allocation

### Requirement: Streaming with partial output on error
Serialization SHALL stream through the sink, and when an error is thrown mid-serialization the destination SHALL retain the partially written output. The runtime SHALL NOT attempt to roll back or buffer the entire payload.

#### Scenario: Error mid-stream leaves partial output
- **WHEN** `jsondm::serialize(foo, fd)` encounters an unserializable value after some bytes were already written
- **THEN** the exception propagates and the bytes written before the error remain on `fd`

### Requirement: Strict JSON numbers
The runtime SHALL throw on attempting to serialize a `NaN` or infinite floating-point value, as these have no JSON representation. It SHALL NOT silently convert them to `null`, strings, or any other value, and SHALL NOT provide feature flags to change this behavior.

#### Scenario: NaN throws
- **WHEN** a struct containing a `double` member equal to `NaN` is serialized
- **THEN** an exception is thrown naming the member path

#### Scenario: Infinity throws
- **WHEN** a struct containing a `float` member equal to infinity is serialized
- **THEN** an exception is thrown naming the member path

### Requirement: Number formatting and parsing
The runtime SHALL serialize integers exactly, and SHALL format floating-point values with the shortest representation that round-trips through deserialization. Parsing SHALL accept valid JSON numbers and reject malformed ones.

#### Scenario: Float round-trip
- **WHEN** a `double` value `v` is serialized and the resulting JSON number is deserialized
- **THEN** the result equals `v` bit-for-bit

#### Scenario: Malformed number rejected
- **WHEN** deserializing input containing `12abc` where a number is expected
- **THEN** an exception is thrown with the offset of the malformed token

### Requirement: String escaping and unescaping
Serialization SHALL escape `"`, `\`, and control characters (using `\uXXXX` for control characters), and deserialization SHALL unescape all JSON escapes including `\uXXXX` surrogate pairs to UTF-8.

#### Scenario: Escaped string serialization
- **WHEN** a struct member `std::string s` contains `a"b\nc` and is serialized
- **THEN** the JSON output contains `"a\"b\nc"`

#### Scenario: Surrogate pair unescaping
- **WHEN** deserializing `"\ud83d\ude00"` into a `std::string` member
- **THEN** the member contains the UTF-8 encoding of U+1F600

#### Scenario: Invalid escape rejected
- **WHEN** deserializing input containing `"\x"` where a string is expected
- **THEN** an exception is thrown identifying the invalid escape

### Requirement: Optional member semantics
For a member of type `std::optional<T>`, an empty optional SHALL cause the key to be omitted from the serialized object, JSON `null` SHALL deserialize to `std::nullopt`, a missing key SHALL leave the member as `std::nullopt`, and a present non-null value SHALL deserialize into the optional. A value whose type does not match `T` SHALL throw.

#### Scenario: Empty optional omits key
- **WHEN** a struct with an empty `std::optional<int>` member is serialized
- **THEN** the output object does not contain that key

#### Scenario: JSON null maps to nullopt
- **WHEN** deserializing `{"opt": null}` into a struct with `std::optional<int> opt`
- **THEN** `opt` is `std::nullopt`

#### Scenario: Missing key leaves nullopt
- **WHEN** deserializing `{}` into a struct with `std::optional<int> opt`
- **THEN** `opt` is `std::nullopt`

#### Scenario: Type mismatch throws
- **WHEN** deserializing `{"opt": "not a number"}` into a struct with `std::optional<int> opt`
- **THEN** an exception is thrown with the member path

### Requirement: Variant member semantics
For a member of type `std::variant<Ts...>`, JSON `null` SHALL deserialize to the `std::monostate` alternative when present, and serialize back to `null`. Non-null values SHALL dispatch to the alternative whose JSON type matches the first token of the value. When two or more alternatives could match the same JSON type (e.g. two struct alternatives), deserialization SHALL throw an ambiguity error.

#### Scenario: Monostate round-trip
- **WHEN** a variant containing `std::monostate` is serialized
- **THEN** the JSON output is `null`

#### Scenario: Type-distinct alternatives dispatch
- **WHEN** deserializing `"hi"` into a `std::variant<std::string, int>` member
- **THEN** the active alternative is the `std::string` one

#### Scenario: Ambiguous struct alternatives throw
- **WHEN** deserializing an object into a `std::variant<Foo, Bar>` member where both `Foo` and `Bar` are structs
- **THEN** an exception is thrown reporting the ambiguity

### Requirement: Container serialization
`std::vector<T>` SHALL serialize to a JSON array, `std::unordered_map<std::string, T>` SHALL serialize to a JSON object, and both SHALL deserialize from their corresponding JSON forms. Empty containers SHALL serialize to `[]` / `{}`.

#### Scenario: Vector round-trip
- **WHEN** a `std::vector<int>` member `{1, 2, 3}` is serialized and the result deserialized
- **THEN** the member is `{1, 2, 3}`

#### Scenario: Empty vector
- **WHEN** an empty `std::vector<int>` member is serialized
- **THEN** the JSON output contains `[]`

### Requirement: Nested structs and recursion guard
Members that are themselves JSON-DM structs SHALL serialize and deserialize recursively. The runtime SHALL enforce a maximum nesting depth (512) and SHALL throw when it is exceeded.

#### Scenario: Nested struct round-trip
- **WHEN** a struct containing another decorated struct member is serialized and the result deserialized
- **THEN** the nested member round-trips

#### Scenario: Depth limit
- **WHEN** deserializing input nested more than 512 levels deep
- **THEN** an exception is thrown reporting the depth limit

### Requirement: Rich error messages
Deserialization errors SHALL include the byte offset in the input, the member path, the expected and found values, and a short context snippet of the input around the error. Serialization errors SHALL include the member path and the reason.

#### Scenario: Parse error context
- **WHEN** deserializing `{"id": 1, "name": "x", "port": "NaN"}` where `port` must be numeric
- **THEN** the exception message contains the offset, the path `port`, "expected number", "found string", and a context snippet

#### Scenario: Serialization error path
- **WHEN** serialization fails on a `double` `NaN` in a nested struct member
- **THEN** the exception message contains the full path such as `settings.announcements[2].port`

### Requirement: fmt formatter adapter
The runtime SHALL provide a `FMT::formatter<T>` base adapter so that decorated structs are formattable via `FMT::format_to` to any output iterator and usable in format strings, delegating to the generated serializer.

#### Scenario: Format to string
- **WHEN** `FMT::format_to(std::back_inserter(s), "{}", foo)` is called with a decorated struct
- **THEN** `s` receives the JSON representation of `foo`

#### Scenario: Struct in log message
- **WHEN** a decorated struct is passed as an argument to `FMT::format("state: {}", foo)`
- **THEN** the formatted message contains the struct's JSON representation

### Requirement: Unknown keys skipped
Deserialization SHALL skip object keys that do not correspond to struct members without error, preserving forward compatibility.

#### Scenario: Unknown key ignored
- **WHEN** deserializing `{"known": 1, "future": 2}` into a struct with only `known`
- **THEN** deserialization succeeds and the extra key is ignored

### Requirement: Positional-array struct mode
A JSON-DM struct SHALL support serializing and deserializing as a JSON array (positional mode), in addition to the default object mode. Array mode SHALL map struct members to array elements in declaration order, with the array length equal to the member count, and SHALL be selected at generation time.

#### Scenario: Array-mode round-trip
- **WHEN** an array-mode struct with members `a`, `b` is serialized
- **THEN** the JSON output is `[a, b]`

#### Scenario: Array-mode deserialization
- **WHEN** input `[1, 2]` is deserialized into an array-mode struct
- **THEN** members receive `1` and `2` in declaration order

#### Scenario: Wrong array length throws
- **WHEN** input with more or fewer elements than members is deserialized into an array-mode struct
- **THEN** an exception is thrown

### Requirement: Runtime definitions are inline
All runtime functions and templates SHALL be defined `inline` in the runtime header such that inclusion in multiple translation units SHALL NOT cause linker errors.

#### Scenario: Multi-TU inclusion
- **WHEN** two translation units include the runtime header and link together
- **THEN** linking succeeds with no duplicate-symbol errors

# json-dm

Typed, allocation-friendly JSON serialization for `rtpmidid`.

## The idea

The schema *is* the C++ struct. Mark a struct with a `/// [JSON-DM]` comment
and `scripts/json_dm_to_cpp.py` generates a serializer/deserializer pair.
The generated code is committed (state machine generator pattern) and relies
on the small inline runtime in `include/rtpmidid/jsondm.hpp`.

- Happy path is allocation-free: numbers use `std::to_chars`/`from_chars`
  (shortest round-trip floats), strings escape/unescape in place, containers
  reuse existing capacity. Allocation is permitted on error paths and when
  target containers must grow ("avoid, not forbid").
- Strict JSON: `NaN`/`Inf` throw (`jsondm::exception`); malformed input
  throws with member path + byte offset + context snippet.
- Streaming: `jsondm::serialize(v, std::string&)` appends; `jsondm::serialize(v, fd)`
  streams straight to a file/socket through the Writer's small inline buffer.
- fmt integration: every decorated struct gets a `FMT::formatter` adapter, so
  structs work in log/error messages and `FMT::format_to` to any sink.

## Marker convention

```cpp
/// [JSON-DM]       // object mode:  {"member": value, ...}
struct foo_t {
  int id;
  std::string name;
  std::vector<int> items;
};

/// [JSON-DM-ARRAY] // positional-array mode: [v0, v1, ...] (e.g. command params)
struct params_t {
  uint32_t peer_id;
};
```

Supported member types: integral types (`int`, `uint*_t`, `size_t`, ...),
`float`, `double`, `bool`, `std::string`, `std::vector<T>`,
`std::unordered_map<std::string, T>`, `std::optional<T>` (empty = key
omitted), `std::variant<Ts...>` (`std::monostate` ↔ JSON `null`; ambiguity
between struct alternatives throws), and other JSON-DM structs (recursion via
`std::vector<Self>`). Nested structs must carry their own marker. `using`
aliases of supported types are resolved. Anything else fails loudly at
generation time, naming the struct and member.

## Regeneration

```sh
python3 scripts/json_dm_to_cpp.py \
  --input src/control_status.hpp \
  --input src/peer_status.hpp \
  --input src/control_commands.hpp \
  --header src --source src
```

Pass every decorated header in one invocation (cross-header references are
resolved across the input set). Generated files are committed:
`<name>_jsondm.hpp` (specialization declarations + formatter adapters) and
`<name>_jsondm.cpp` (definitions), added to `src/CMakeLists.txt`. Run the
generator and commit the output whenever a decorated struct changes; the
generator output is deterministic (byte-identical regeneration).

## API

```cpp
std::string out;
jsondm::serialize(foo, out);            // append JSON to out
jsondm::serialize(foo, fd);             // stream to a file descriptor
foo_t back;
jsondm::deserialize(out, back);         // throws jsondm::exception on error
```

## Control socket

The control socket command registry is typed (`command_entry_t` with
per-command `ParamsT`/`ResultT`); requests are dispatched by a Reader
pre-scan of `method`, and responses are composed from typed parts in the
Writer. The wire protocol is byte-compatible with the pre-json-dm protocol
(see `openspec/changes/add-json-dm/wire-inventory.md` for the captured
shapes). nlohmann/json is removed; JSON exists only at the wire boundary.

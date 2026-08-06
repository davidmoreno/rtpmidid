## Why

The control socket builds every JSON response by hand with nlohmann (`json_t` objects assembled per handler), which allocates heavily, duplicates the shape of the data it already has in structs, and is error-prone. INI config parsing (`ini.cpp`) is likewise hand-written with hard-coded section rules. We want the schema to *be* the C++ struct: mark a struct with a `/// [JSON-DM]` comment, and a generator produces an optimal, allocation-free-on-the-happy-path serializer/deserializer — with streaming output, strict JSON semantics, and rich errors, without adding any new dependencies.

## What Changes

- **New generator** `scripts/json_dm_to_cpp.py` (sibling of `statemachine_to_cpp.py`): scans input headers for `/// [JSON-DM]`-marked structs, parses member declarations (pure Python stdlib, balanced brace/angle-bracket tokenizing), validates every member type, and fails loudly at generation time on anything unsupported.
- **New inline runtime header** (`include/rtpmidid/jsondm.hpp`, the "common.hpp" from exploration): `Writer` (inline buffer + sink), `Reader` (`string_view` input), primitives, containers, fmt adapter, and the public API — all `inline`, ODR-safe.
- **Generated output per decorated header**: `*_jsondm.hpp` (specialization declarations + `FMT::formatter<T>` adapters) and `*_jsondm.cpp` (definitions), committed to the repo following the state machine generator pattern. No build-time codegen.
- **Public API**: `serialize(const T&, std::string&)`, `serialize(const T&, int fd)` (streaming straight to disk/network), `deserialize(std::string_view, T&)`.
- **fmt integration**: `FMT::formatter<T>` specializations so structs work with `FMT::format_to` (any output iterator) and inside log/error messages — reusing the existing fmt dependency (no new dependencies).
- **Strict JSON**: `NaN`/`Inf` cannot be serialized → throw; no silent conversion, no flags (YAGNI).
- **Rich errors**: member path + JSON offset + context snippet + expected/got. Partial output on the destination is allowed if an error occurs mid-serialization (streaming stance).
- **Type support**: primitives, `std::string`, `std::vector<T>`, `std::unordered_map<std::string,T>`, `std::optional<T>` (undefined → key omitted), `std::variant<Ts...>` (`std::monostate` ↔ JSON `null`; dispatch by JSON type; ambiguity between struct alternatives → throw), nested JSON-DM structs (recursion guarded).
- **INI backend**: the generated traversal is format-agnostic; an INI backend maps object → section, `key = value`, `std::vector` member → repeatable sections, `std::optional<T>` member → optional section.
- **Control socket migration**: shaped responses (e.g. `status`, `mdns_status`) become JSON-DM structs serialized directly to the socket fd; the command registry becomes typed (`command_t<ParamsT, ResultT>`); requests/responses are composed from typed structs.
- **Typed midipeer interface**: `midipeer_t::status()` returns a typed `peer_status_t` envelope (common fields + `std::optional` per-type detail subtrees); `peer->command(cmd, params)` passes raw JSON only at that interface boundary and parses internally into per-peer typed structs. No dynamic JSON value type exists anywhere.
- **Complete nlohmann removal**: nlohmann/json is removed entirely — `third_party/nlohmann/`, `src/json.hpp`, `src/json_fwd.hpp`, and the `json_t` alias disappear. JSON exists only as the wire format at the control socket boundary.
- **Wire protocol preserved**: the control socket wire protocol is kept byte-compatible — request/response envelopes (`{"method", "params", "id"}` / `{"id", "result"}` / `{"id", "error"}`), per-command params forms (arrays and objects), result shapes (including bare-string results like `"ok"`), and peer status shapes (per-type key names like RTP's `peer` subtree) are reproduced exactly by typed structs designed around the existing shapes. No CLI changes, no `docs/CONTROL.md` protocol changes.

## Capabilities

### New Capabilities

- `json-dm-generator`: Python script behavior — marker discovery, struct/member parsing, supported type set, fail-loud validation, generated file layout.
- `json-dm-runtime`: The C++ runtime library — Writer/Reader, primitives/containers, optional/variant semantics, strict JSON error model, fmt adapter, public API.
- `midipeer-typed-interface`: Redesign of the `midipeer_t` interface to typed structs — `status()` returning a `peer_status_t` envelope with per-type detail subtrees, and `command()` with raw JSON only at the interface boundary.
- `json-dm-ini`: INI backend mapping generated traversal to INI sections/keys, including repeatable and optional sections.
- `control-socket-jsondm`: Typed control socket command registry, request/response composition from typed structs, direct fd streaming, and wire-protocol preservation (byte-compatible shapes reproduced by structs).

### Modified Capabilities

- None.

## Impact

- **Code**: new `scripts/json_dm_to_cpp.py`; new `include/rtpmidid/jsondm.hpp`; generated `*_jsondm.hpp`/`.cpp` committed next to decorated headers; `src/control_socket.cpp` response paths rewritten; `src/settings.hpp` structs decorated for INI use (deferred to the ini capability).
- **Build**: generated `.cpp` files added to `target_sources` (same pattern as `rtpclient_statemachine.cpp`); no new CMake machinery, no build-time python.
- **Dependencies**: none added; nlohmann is removed. fmt (already a dependency via pkg-config) and the existing `FMT::` layer in `formatterhelper.hpp` are reused.
- **Behavior**: the control socket wire protocol is byte-compatible — same shapes, same framing (newline-delimited), same command set. Internal implementation is fully typed. The CLI and `docs/CONTROL.md` need no protocol changes. Strictness change: `NaN`/`Inf` now throw instead of serializing garbage.

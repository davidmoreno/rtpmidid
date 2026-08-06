## Context

The control socket (`src/control_socket.cpp`) builds every JSON response by hand as `nlohmann::json` objects, allocating per response and duplicating data shapes that already exist as C++ structs. INI config parsing (`src/ini.cpp`) is hand-written with hard-coded "unique vs repeatable" section rules. The project already has a mini fmt layer (`include/rtpmidid/formatterhelper.hpp`, `FMT::` namespace, switchable to system fmt via pkg-config) and a codegen house pattern (`scripts/statemachine_to_cpp.py` generates `.hpp` + `.cpp` that are committed, e.g. `lib/rtpclient_statemachine.cpp`).

The goal: a comment-decorated struct (`/// [JSON-DM]`) is the schema. A Python generator emits an optimal, per-struct serializer/deserializer; a small inline runtime header provides common code. No new dependencies. Strict JSON semantics with rich errors.

## Goals / Non-Goals

**Goals:**
- Schema = the C++ struct, decorated only by a `/// [JSON-DM]` comment. No macros in structs, no separate schema language.
- Optimal happy path: no memory allocation, no dynamic dispatch in generated code, `to_chars`/`from_chars` numbers.
- Streaming serialization directly to destination (string, fd/socket, any fmt output iterator) with only a small internal buffer.
- Strict JSON: `NaN`/`Inf` throw; no silent conversions; no feature flags.
- Rich errors: member path + offset + context snippet + expected/got.
- Format-agnostic traversal so an INI backend can reuse the same generated code.
- Reuse existing dependencies (fmt); add zero new ones.

**Non-Goals:**
- Build-time codegen: generated files are committed; the script runs manually (statemachine pattern).
- Runtime reflection / generic (non-generated) serialization of arbitrary structs.
- Binary serialization format.
- Memory arenas or custom allocators; "avoid, not forbid" allocation is the bar.
- Schema versioning / forward-compat negotiation.
- Strict-mode unknown-key errors (unknown keys are skipped, standard JSON behavior).
- A dynamic JSON value type (DOM): internally everything is typed structs; JSON exists only as the wire format at the control socket boundary (D17–D21).

## Decisions

### D1. Fully-specialized generated code (python resolves types at generation time)

Each member's type is resolved by the Python script, so generated code calls concrete library functions — no templates in the hot path, no dispatch:

```cpp
template <> inline void serializer<Foo>::write(const Foo& v, Writer& w) {
    w.obj_begin();
    w.push_key("id");    write_int(w, v.id);
    w.push_key("name");  write_str(w, v.name);
    w.obj_end();
}
```

*Alternatives considered:* metadata table (`constexpr` tuple of member pointers) + generic engine — less generated code and centralized error handling, but generic key-dispatch and member-dispatch overhead, and harder to keep optimal. Rejected: codegen advantage is exactly that python does type resolution.

### D2. Format-agnostic traversal; JSON and INI are backends

Generated code calls a fixed op set (`obj_begin/obj_end/key/arr_begin/arr_end` + scalar writes), not JSON-specific functions. INI maps the same ops: object → `[section]`, scalar → `key = value`, vector member → repeatable sections, optional member → optional section. This is what makes ini-dm a backend, not a fork.

### D3. fmt integration via an adapter, not via fmt-native codegen

`FMT::formatter<Foo>` must be complete where `format_to` is instantiated (compile-time `parse()`), which forces header code. So the generated header contains only a one-line adapter; the real code stays in the generated `.cpp`:

```cpp
// generated .hpp
template <> struct FMT::formatter<Foo> : jsondm::formatter_base<Foo> {};
// common.hpp (inline): formatter_base<T>::format → Writer{ctx.out()} → serializer<T>::write
```

This gives `FMT::format_to(any_iterator, "{}", foo)` (string, fd, socket) and structs inside log/error messages, while keeping the heavy per-member code in `.cpp` with direct calls. One type-erased dispatch per struct — negligible.

*Alternatives considered:* (a) fmt-native codegen — generated `format()` bodies using `FMT::format_to` per member; forces everything into headers and tangles JSON escaping with fmt spec parsing. (b) no fmt at all — loses log-message integration and format-to-anything; rejected, fmt is already a dependency.

### D4. Streaming Writer: inline buffer + sink; partial output allowed

`Writer` holds a small inline stack buffer and flushes to a sink (std::string append, fd `write()`). Serialization streams; there is no measure pass and no big intermediary buffer. If an error is thrown mid-serialization, the destination keeps whatever was written (explicit stance — partial responses allowed). For the control socket's newline-delimited protocol, the trailing `\n` is written only on success, so a truncated line is self-signaling.

*Alternatives considered:* two-pass measure-then-write (exact sizing, transactional) — rejected as the default: costs double traversal and the sink grows anyway; a `measure()` may be added later for exact `reserve()`.

### D5. API

- `jsondm::serialize(const T&, std::string&)` — append JSON (reuses capacity across calls).
- `jsondm::serialize(const T&, int fd)` — stream directly to a fd (file, unix socket).
- `jsondm::deserialize(std::string_view, T&)` — zero-copy input, throws on error.
- Everything else (`serializer<T>::write/read`, formatter) is internal.

`std::string_view` input is free (no ownership, no copy); `std::string&` output grows in place. The earlier "raw char buffer" idea is superseded — the fmt/sink model makes it unnecessary.

### D6. Error model: exceptions with maximum information

Errors are `rtpmidid::exception`-style throws (project idiom, e.g. `ini_exception`). Messages include: member path, offset, context snippet (~40 chars), expected/got, depth. Path tracking is a fixed array of `string_view` (depth ≤ 32) on the Writer/Reader; generated code emits `push_key("...")` / `push_idx(n)` and pops after the member. The throw site formats the message before unwinding, so the path is intact. Error paths allocate (messages); the happy path does not.

Examples:
- `JSON serialization error at settings.announcements[2].port: value is NaN, not representable in JSON`
- `JSON parse error at offset 128 (settings.announcements[2].port): expected number, found string — near "...23, "port": "x1"`

### D7. Numbers: `std::to_chars` / `std::from_chars`

Shortest round-trip floats, no locale, no allocation. `NaN`/`Inf` have no JSON representation → throw (no silent `null` conversion, no flags; flags are YAGNI until a real caller needs them).

### D8. `std::optional<T>` = undefined (key omitted)

- Empty optional → key is omitted entirely.
- JSON `null` → `nullopt`; missing key → `nullopt`.
- Type mismatch → throw.

### D9. `std::variant<Ts...>` = typed alternatives

- `std::monostate` alternative ↔ JSON `null`.
- Otherwise dispatch on the first JSON token (string vs number vs bool vs object/array).
- Two object alternatives (both structs) are ambiguous → throw with a clear message. This constraint is accepted for v1; a discriminator-key convention can come later if a real member needs it.

### D10. Parser: pure Python stdlib, fail loud

`scripts/json_dm_to_cpp.py` scans lines for `/// [JSON-DM]`, brace-counts to the struct body, tokenizes members with balanced `()` `{}` `<>` tracking (nested templates like `std::unordered_map<std::string, std::vector<Foo>>`), skips comments and default initializers. Every member type must classify into the supported set (primitives, `std::string`, `std::vector<T>`, `std::unordered_map<std::string,T>`, `std::optional<T>`, `std::variant<Ts...>`, JSON-DM struct); anything else is a hard generation error naming the struct and member. Nested structs must carry their own `/// [JSON-DM]` marker (no implicit generation) — this includes the nested structs inside `settings_t`.

### D11. Generated files committed (statemachine pattern)

`scripts/json_dm_to_cpp.py --input foo.hpp --header <dir> --source <dir>` emits `foo_jsondm.hpp` (specialization declarations + `FMT::formatter` adapters) and `foo_jsondm.cpp` (definitions). Files are committed and added to `target_sources` — no build-time python, no CMake custom commands.

### D12. Namespace and layout

- Runtime namespace `jsondm` (top-level — json-dm is a subproject that may eventually live outside the repo).
- Runtime header `include/rtpmidid/jsondm.hpp` ("common.hpp" from exploration), all definitions `inline`, ODR-safe.
- Generated code uses `jsondm::` and includes the runtime header; public API lives in the runtime header (inline templates calling non-inline `serializer<T>::write` declared in generated headers).

### D13. Recursion and depth

Nested JSON-DM structs, `vector<Self>`, `map<string, Self>` are supported; Writer/Reader track a depth counter (limit 512) and throw on exceed — unbounded nesting would otherwise blow the stack.

### D14. Strings

Write: JSON escaping (quote, backslash, control chars → `\uXXXX`) into the sink. Read: unescape into the member's `std::string` in place (reuses capacity), including surrogate pairs → UTF-8. No forced `string_view` members — zero-copy strings are a possible future opt-in, but escapes and lifetime hazards make owned strings the right default ("avoid, not forbid").

### D15. Unknown keys are skipped

Standard JSON forward-compat behavior: a newer client talking to an older server must not break. (Strict mode is a possible later flag.)

### D17. No dynamic JSON value type — fully typed internals, JSON only at boundaries

`midipeer_t::status()`/`command()` are `json_t`-typed virtual interface methods, and peer status payloads are heterogeneous (`peer.latency_ms.average` for RTP peers, `name`/`port` for ALSA, `name`/`device`/`status` for rawmidi). A dynamic JSON DOM would replace nlohmann with another allocation-bearing untyped surface — contradicting the goal. Instead, the internal model is fully typed: every payload is a struct, and the wire protocol is preserved by designing structs that reproduce the existing shapes exactly (D18–D21). The known wire consumer is the in-repo CLI; it needs no changes.

### D18. Typed status interface preserving the wire shape

`midipeer_t::status()` returns `std::variant<rtp_peer_status_t, alsa_peer_status_t, rawmidi_peer_status_t, ...>` where each alternative is the *flat wire entry* — the router-assigned common members (`id`, `send_to`, `stats`, `type`) followed by that peer type's exact current fields and key names (RTP: `name`, `peer{latency_ms, status, local, remote}`; ALSA: `name`, `port`; rawmidi: `name`, `device`, `status`). Serializing the variant emits the active alternative's object — byte-compatible with today's router entry. The router assigns the common members via a generic `std::visit` lambda (every alternative declares `id`/`send_to`/`stats`/`type`, so a new peer type that omits them is a compile error). The status variant is serialize-only — the daemon never deserializes peer status (clients do, in Python), so the runtime's struct-struct ambiguity rule (D9) does not apply.

*Alternatives considered:* (a) one shared envelope struct with `std::optional` fields for every peer type's keys (`peer?`, `port?`, `device?`, `status?`): also reproduces the wire via optional omission and is bidirectional, but grows as a union of all peer fields and risks key-name collisions across peer types; rejected in favor of type isolation. (b) a per-type detail subtree nested under a generic key: changes the wire; rejected.

### D19. Typed command dispatch

Requests are already RPC-shaped (`{"method", "params", "id"}`). The command registry becomes typed: `command_t<ParamsT, ResultT> { name, description, ResultT (*func)(control_socket_t&, const ParamsT&) }`. Dispatch: the Reader pre-scans the raw request bytes to read `method` (save/restore position), finds the registry entry, then deserializes the per-command request struct `{id, params}` — with `method` skipped as an unknown key. The request `id` is `std::optional<std::string>` because today's requests may omit it (the CLI sends no id; the daemon echoes `null`). Responses are composed from typed parts in the Writer (`obj_begin; key "id"; write optional string; key "result"; serializer<ResultT>::write; obj_end`), reproducing the current `{"id", "result"}` / `{"id", "error"}` envelopes. Errors are exceptions caught at the boundary.

### D20. Peer command boundary: raw JSON only at the interface

Peer-specific commands (`<peer_id>.<cmd>`) keep the generic string dispatch on the interface, but the payload becomes raw JSON at the boundary: `virtual std::string command(const std::string& cmd, std::string_view params_json)` — implementations parse `params_json` internally into their own typed command structs and serialize typed results back. This keeps the interface open to per-peer command sets without a DOM and without interface bloat. The wire bytes a peer receives and returns are unchanged.

*Alternatives considered:* per-command typed virtuals on `midipeer_t` (`virtual X set_name(const set_name_params_t&)`): every subclass must override every virtual (interface bloat, open/closed violation); rejected.

### D21. Wire protocol preservation — structs reproduce the existing shapes

The wire protocol is a contract and SHALL remain byte-compatible. Two mechanisms make full typing compatible with the legacy shapes:

- **Positional-array struct mode**: commands whose params are JSON arrays (`router.remove` → `[id]`, generic CLI positional commands) use a struct serialized as a JSON array (`/// [JSON-DM-ARRAY]` marker — a struct ↔ array mapping in member order, array length = member count). Object-params commands use ordinary object structs.
- **Legacy multi-form params**: `connect` accepts four forms today (`[hostname]`, `[hostname, port]`, `[name, hostname, port]`, `{name, hostname, port}`) with normalization logic (`name` defaults to `hostname`, default port). No single struct can express that; the `connect` entry uses a small hand-written params reader built on the public Reader API — typed code, no DOM, boundary-local.

Result shapes are reproduced per command (`"ok"` string results → `ResultT = std::string`; `["ok"]` array results → a faithful model). The full wire-shape inventory (every command's params forms and result shapes, peer status keys/order) is a migration task; byte-compatibility is enforced by tests that serialize the new structs and compare against captured legacy payloads. The CLI and `docs/CONTROL.md` need no protocol changes.

### D16. INI compatibility is a constraint, not a nice-to-have

User config files are long-lived. The generated INI reader must round-trip the semantics covered by the hand-written parser and `test_settings.cpp`. The hand parser is kept until the generated one passes those semantics.

## Risks / Trade-offs

- **Parser fragility on real C++** (nested structs in `settings.hpp`, default initializers, templates) → Constrained supported type set, balanced-token parser, fail-loud validation, parser unit tests against the actual headers. Mitigation if needed: tree-sitter (would be a new dependency — last resort).
- **Cross-TU non-inlining** of generated `serializer<T>::write` (defined in `.cpp`) → The library primitives are inline in the header; generated bodies are one call per member, so the loss is one indirection per member. Revisit with `always_inline` or header generation if profiling demands.
- **`FMT::formatter` must be header-complete** → Solved by the adapter base (`formatter_base<T>` inline in the runtime header); generated header stays one line per struct.
- **Partial output on serialization error** → Accepted stance. Control socket: trailing `\n` only on success makes truncation self-signaling; handler sends an error response after catching.
- **`std::variant` struct-struct ambiguity** → Throws with a clear message; accepted constraint for v1.
- **INI round-trip of real user configs** → Keep hand parser until generated reader passes `test_settings.cpp` semantics; no silent config regression.
- **`std::unordered_map` serialization order is nondeterministic** (hash order) → Accept for transport; prefer struct members (fixed order) in response payloads; deterministic output matters only for tests/caching and can be addressed later if needed.
- **fmt/std::format drift** (`USE_LIBFMT` switch) → The adapter touches only `formatter<T>`, stable across both; runtime otherwise never depends on fmt.
- **`to_chars` float round-trip availability** depends on stdlib maturity → Verify with the actual toolchain (clang++ + system libstdc++) in a spike; fallback is a small Ryu-style implementation if the stdlib is deficient.
- **Legacy wire quirks must be reproduced faithfully** (`"ok"` vs `["ok"]` results, `"id": null` echoes, `connect`'s four params forms) → The wire-shape inventory task captures every shape; byte-compat tests serialize new structs and compare against captured legacy payloads. Any quirk that cannot be modeled by a struct gets a boundary-local hand-written reader (public Reader API, typed, no DOM).
- **`peer_status` variant is serialize-only** (struct-struct ambiguity rule would reject deserialization) → Acceptable: the daemon never deserializes peer status; clients consume it in Python. Documented in D18.
- **Wide interface churn** (`midipeer_t` and every peer implementation change together) → Migrate peer types one at a time; keep the tree green by landing the interface change with all implementations in the same commit.

## Migration Plan

1. **Spike**: verify `to_chars`/`from_chars` float round-trip on the project toolchain; verify the `FMT::formatter` adapter compiles with the `USE_LIBFMT`/std::format switch.
2. **Runtime**: write `include/rtpmidid/jsondm.hpp` (Writer/Reader/primitives/containers/optional/variant/array-mode structs/fmt adapter/public API) + unit tests.
3. **Generator**: `scripts/json_dm_to_cpp.py` + parser tests against `src/settings.hpp` and a fixture header (object and array mode).
4. **First struct end-to-end**: decorate a small response struct, generate, commit, test round-trip.
5. **Wire-shape inventory**: capture every command's params forms and result shapes, peer status keys/order, and the request/response envelope behavior (including `"id": null` echoes) as captured legacy payloads for byte-compat tests.
6. **Typed interface redesign**: define per-type status structs reproducing current shapes; change `midipeer_t::status()` (variant) and `command()` (raw JSON boundary) signatures; migrate every peer implementation (RTP, ALSA, rawmidi, listener) one at a time.
7. **Control socket boundary**: typed `command_t<ParamsT, ResultT>` registry, method pre-scan, request/response composition from typed structs, positional-array params structs, hand-written reader for legacy multi-form params (`connect`), direct fd streaming; byte-compat tests against the captured payloads.
8. **nlohmann removal**: delete `third_party/nlohmann/`, `src/json.hpp`, `src/json_fwd.hpp`; confirm no includes remain; update `tests/test_midirouter*.cpp` assertions to typed structs.
9. **INI (separate capability)**: `settings_t` decoration, INI backend, keep hand parser until generated reader passes `test_settings.cpp` semantics.

Rollback: generated files are committed and reviewed; steps 6–8 land as one coherent unit (interface, boundary, and removal must change together to keep the tree green). The CLI and `docs/CONTROL.md` are untouched by protocol behavior.

## Spike Results (implementation task group 1)

Verified on this machine (Fedora, g++ 16.1.1, clang++ 22.1.8, libstdc++):

- **1.1 `to_chars`/`from_chars`**: double and float shortest-repr round-trip verified byte-exact under C++17 and C++20 on both compilers (`0.1` → `"0.1"`, `1/3` → `"0.3333333333333333"`). `from_chars` is prefix-based (parses the longest valid prefix — the JSON reader must pass exactly the number token span). `1e999` → `result_out_of_range`. `NaN`/`Inf` serialize as `"nan"`/`"inf"` — NOT valid JSON, so the serializer SHALL pre-check values (isnan/isinf) and throw before formatting.
- **1.2 fmt adapter**: `template<> struct FMT::formatter<T> : formatter_base<T> {}` (empty derived + inline template base with constexpr `parse` and `format`) compiles and works with `std::format` on both compilers, across translation units (no ODR/link issues), and embedded in larger format strings. Real fmt (`USE_LIBFMT`) is NOT installed on this machine (project builds C++20 + `std::format` here); the fmt path is structurally identical (`fmt::formatter`) and should be verified in a fmt-enabled build before relying on it.

## Open Questions

- Should the control socket `status` response expose the *whole* `settings_t` (once decorated) or keep a projection struct? (Projection keeps API shape stable; whole-settings is less code. Decide during migration.)
- Do we want `measure()`/`reserve()` as an optional two-pass API for large payloads later? (Not needed for v1; sink growth suffices.)
- Strict unknown-key mode: keep skip-only (default) or add a flag? (Skip-only for v1.)
- Exact runtime header location: `include/rtpmidid/jsondm.hpp` now, `third_party/jsondm/` if/when extracted as a subproject.
- Array-mode marker spelling: `/// [JSON-DM-ARRAY]` vs an option on the existing marker. (Prefer a distinct marker; confirm during implementation.)
- Whether any other command besides `connect` needs a hand-written params reader — resolved by the wire-shape inventory.

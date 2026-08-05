## 1. Spike: toolchain verification

- [x] 1.1 Verify `std::to_chars`/`std::from_chars` float round-trip (shortest repr, NaN/Inf handling) on the project toolchain (clang++ + system libstdc++, C++17 and C++20)
- [x] 1.2 Verify the `FMT::formatter<T>` adapter pattern compiles under both `USE_LIBFMT` (system fmt) and the `std::format` fallback in `formatterhelper.hpp`
- [x] 1.3 Record spike results in the change (any stdlib gaps and their fallbacks)

## 2. Runtime library

- [x] 2.1 Create `include/rtpmidid/jsondm.hpp` with the `jsondm` namespace, `Writer` (inline buffer + sink, depth counter, path stack) and `Reader` (string_view input, offset, depth, path stack)
- [x] 2.2 Implement scalar primitives: `write_int/uint/bool/float` and read counterparts using `to_chars`/`from_chars`, throwing on NaN/Inf
- [x] 2.3 Implement string write with JSON escaping and read with unescaping (including `\uXXXX` surrogate pairs to UTF-8), growing the target `std::string` in place
- [x] 2.4 Implement container support: `std::vector<T>` (array) and `std::unordered_map<std::string,T>` (object) write/read loops
- [x] 2.5 Implement `std::optional<T>` semantics: omit key when empty, `null` → nullopt, missing key → nullopt, type mismatch throws
- [x] 2.6 Implement `std::variant<Ts...>` semantics: monostate ↔ `null`, dispatch by first JSON token, ambiguity (multiple struct alternatives) throws on deserialize; serialization visits the active alternative
- [x] 2.7 Implement positional-array struct mode: struct ↔ JSON array mapping in member order (length must equal member count) — via arr/next_elem/index-guard primitives; emitted shape lands with the generator
- [x] 2.8 Implement recursion depth guard (limit 512) and rich error messages: member path + offset + context snippet + expected/got; error path may allocate
- [x] 2.9 Implement the public API: `serialize(const T&, std::string&)`, `serialize(const T&, int fd)` (fd sink), `deserialize(std::string_view, T&)` (inline templates calling generated `serializer<T>::write`/`deserializer<T>::read`)
- [x] 2.10 Implement `formatter_base<T>` adapter so `FMT::formatter<T>` works for decorated structs via `FMT::format_to`
- [x] 2.11 Ensure every runtime definition is `inline`/ODR-safe; add a multi-TU compile test

## 3. Runtime tests

- [x] 3.1 Unit tests: scalar round-trips (int/uint/float/double/bool), float shortest-repr round-trip, malformed numbers throw with offset
- [x] 3.2 Unit tests: string escaping/unescaping, surrogate pairs, invalid escapes throw
- [x] 3.3 Unit tests: vector/map round-trips, empty containers, unknown keys skipped
- [x] 3.4 Unit tests: optional semantics (omit/null/missing/mismatch), variant semantics (monostate/dispatch/ambiguity throws on deserialize, visit on serialize)
- [x] 3.5 Unit tests: positional-array mode round-trip and wrong-length errors
- [x] 3.6 Unit tests: nested struct round-trip, depth limit, rich error messages content (path/offset/context)
- [x] 3.7 Unit tests: happy-path allocation checks (reused string / reused struct), partial output left on fd sink on error
- [x] 3.8 Unit tests: fmt adapter (`FMT::format_to` to string, struct inside a format string)

## 4. Generator

- [x] 4.1 Create `scripts/json_dm_to_cpp.py` with CLI (`--input` repeatable, `--header`, `--source`) and generated-file path reporting
- [x] 4.2 Implement marker discovery (`/// [JSON-DM]` object mode, `/// [JSON-DM-ARRAY]` positional-array mode, unknown markers fail loudly) and struct body parsing with balanced brace/angle-bracket tokenizing, skipping comments and default initializers
- [x] 4.3 Implement member type classification for the supported set (primitives, `std::string`, `std::vector<T>`, `std::unordered_map<std::string,T>`, `std::optional<T>`, `std::variant<Ts...>`, JSON-DM structs, recursion)
- [x] 4.4 Implement fail-loud validation: unsupported types and unmarked nested structs abort with struct+member named
- [x] 4.5 Emit `<name>_jsondm.hpp` (specialization + `FMT::formatter<T>` adapter declarations) and `<name>_jsondm.cpp` (definitions) with deterministic, declaration-order output
- [x] 4.6 Add generator tests: fixture headers covering all supported types, both markers, unsupported types, nested structs, initializers, nested templates; byte-identical regeneration

## 5. First end-to-end slice

- [x] 5.1 Decorate a small response struct, generate, commit the generated pair
- [x] 5.2 Add the generated `.cpp` to `target_sources` (statemachine pattern)
- [x] 5.3 Round-trip test through the full stack (struct → JSON → struct) in the test suite

## 6. Wire-shape inventory

- [x] 6.1 Capture every control socket command's params forms and result shapes (including `"ok"` vs `["ok"]` variants and `"id": null` echoes) as captured legacy payloads
- [x] 6.2 Capture every peer type's status keys and key order (RTP `name`/`peer{...}`, ALSA `name`/`port`, rawmidi `name`/`device`/`status`) and the router enrichment fields
- [x] 6.3 Identify which commands need positional-array params structs and which need a hand-written params reader (`connect` and any other multi-form commands)
- [x] 6.4 Define the per-command `ParamsT`/`ResultT` structs that reproduce the captured shapes

## 7. Typed midipeer interface

- [x] 7.1 Define per-type status structs (router members `id`/`send_to`/`stats`/`type` + each peer type's exact fields) — all `/// [JSON-DM]`-decorated
- [x] 7.2 Replace `peer_status()` in `utils.cpp` with typed latency/local/remote structs
- [x] 7.3 Change `midipeer_t::status()` to return `std::variant<per-type status...>`; migrate every peer implementation (RTP, ALSA, rawmidi, listeners) one at a time
- [x] 7.4 Change `midipeer_t::command()` to `command(cmd, std::string_view params_json)`; migrate implementations to parse into their own typed command structs and return serialized results with unchanged wire bytes
- [x] 7.5 Rework `midirouter_t::status()` to aggregate `std::vector<variant>` with common members set via generic `std::visit`
- [x] 7.6 Update `tests/test_midirouter*.cpp` and other tests from JSON assertions to typed struct assertions
- [x] 7.7 Verify peer status serialization byte-matches the captured legacy payloads

## 8. Control socket boundary

- [x] 8.1 Rework the command registry to typed `command_t<ParamsT, ResultT>` entries (typed handler signatures)
- [x] 8.2 Implement request dispatch: Reader pre-scan for `method` (save/restore position), typed entry selection, deserialize per-command `{id, params}` request struct (`id` optional; method skipped as unknown key)
- [x] 8.3 Implement response composition from typed parts in the Writer (`{"id", "result"}` / `{"id", "error"}`, id echoed as `null` when absent) with exceptions caught at the boundary
- [x] 8.4 Implement positional-array params structs for array-form commands (`router.remove`, generic CLI positional commands)
- [x] 8.5 Implement the hand-written params reader for `connect` (four legacy forms + normalization) on the public Reader API
- [x] 8.6 Serialize responses directly to the client fd with the newline terminator written only on success; keep partial output on error
- [x] 8.7 Byte-compat tests: new serialization compared against captured legacy payloads for every command and status shape; verify the unmodified CLI works end-to-end against the migrated daemon

## 9. nlohmann removal

- [x] 9.1 Remove `third_party/nlohmann/`, `src/json.hpp`, `src/json_fwd.hpp`; confirm no nlohmann includes or symbols remain anywhere
- [x] 9.2 Full build + test suite pass without nlohmann

## 10. INI backend

- [x] 10.1 Implement the INI writer/reader backends over the shared traversal ops (object → section, scalar → `key = value`)
- [x] 10.2 Implement repeated sections from `std::vector<T>` members and optional sections from `std::optional<T>` members
- [x] 10.3 Implement INI error reporting with file name and line number (consistent with `ini_exception`)
- [x] 10.4 Decorate `settings_t` (and nested structs) with `/// [JSON-DM]`; generate and commit
- [x] 10.5 Compatibility tests: generated reader matches hand-written parser results on real configs (incl. repeated sections, whitespace, comments); generated writer output loadable by hand parser
- [x] 10.6 Keep the hand-written parser active until compatibility passes; only then switch and update `test_settings.cpp` expectations if any

## 11. Documentation and cleanup

- [x] 11.1 Document the json-dm workflow in the repo (marker convention, generator usage, regeneration steps)
- [x] 11.2 Note the internal change in `docs/CONTROL.md` (protocol behavior unchanged; internal implementation now typed)

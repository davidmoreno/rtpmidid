# dm-json framework

rtpmidid uses a custom JSON stack (dm-json) for control RPC, WebSocket, and
status serialization. It replaced nlohmann/json. Migration history:
[PROGRESS.md](../../PROGRESS.md).

## Components

| Piece | Location |
|-------|----------|
| Runtime (parser/writer) | `include/rtpmidid/dm_json/runtime.hpp`, `lib/dm_json/runtime.cpp` |
| Annotated type headers | `src/dm_json_status.hpp`, `src/dm_json_rpc.hpp` |
| Generator | `scripts/dm_json_gen.py` |
| Generated code | `build/.../dmjson_gen/dm_json_generated.{hpp,cpp}` |
| CMake helper | `cmake/DmJson.cmake` → `rtpmidid-dmjson` static library |

## Annotating structs

Mark serializable structs with `// [dm-json]` above the struct definition.
Per-field options go on the same line as the field:

```cpp
// [dm-json]
struct connect_params_t {
  std::optional<std::string> name;     // [dm-json: omit_if_null]
  std::string hostname;
  std::optional<std::string> port;     // [dm-json: omit_if_null]
};
```

Common field options:

| Option | Effect |
|--------|--------|
| `omit_if_null` | Skip optional fields when null/absent |
| `omit_if_empty` | Skip empty strings/vectors |
| `opaque` | Emit pre-serialized JSON verbatim (RPC envelopes) |

Only structs in the generator input files are emitted. Current inputs:
`dm_json_status.hpp`, `dm_json_rpc.hpp`.

## Build integration

`cmake/DmJson.cmake` defines `dmjson_add_library(rtpmidid-dmjson)`:

1. Runs `dm_json_gen.py` at build time
2. Compiles `runtime.cpp` + generated `dm_json_generated.cpp`
3. Exposes includes: `include/`, `src/`, `dmjson_gen/`

Regenerate goldens after intentional generator changes:

```bash
make test-gen
```

See [PROGRESS.md](../../PROGRESS.md) for the manual regen recipe.

## Runtime API

### Writer

`dmjson::writer_t` — stack-friendly JSON builder used in handlers and status
serialization.

### RPC envelope

`dmjson::rpc::scan_envelope(line)` parses:

```json
{"method": "status", "params": {}, "id": 1}
```

Returns method name, raw params JSON, and raw id JSON.

Response helpers in `src/dm_json_rpc.hpp`:

- `rpc_result_t` — `{"id":…, "result":…}`
- `rpc_error_envelope_t` — `{"id":…, "error":"…"}`

### Generated serializers

For each annotated struct, the generator emits `to_json(writer, …)` and
`from_json(reader, …)` in `dm_json_generated.hpp`.

Used by `control_rpc_dispatch_line()` to parse params and serialize results.

## Recipe: add a new RPC method

1. **Define params/result structs** in `src/dm_json_rpc.hpp` with `// [dm-json]`
2. **Rebuild** — generator picks up new types automatically
3. **Add handler** in `src/control_rpc.cpp`:
   - Parse params with generated `from_json`
   - Return `rpc_result_t` with serialized result, or `rpc_error_envelope_t`
4. **Register** in `build_help_entries()` and the dispatch `if` chain
5. **Test** in `tests/test_dm_json_generated.cpp` (round-trip) and/or
   integration tests

## Recipe: add a status field

1. Add field to the relevant struct in `src/dm_json_status.hpp`
2. Populate it in the peer/router code that fills `router_peer_row_t` etc.
3. Rebuild; frontend TypeScript types may need a manual update in
   `frontend/src/tabs/types.ts`

## Tests

| Test | Purpose |
|------|---------|
| `tests/test_dm_json_runtime.cpp` | Envelope parsing, writer edge cases |
| `tests/test_dm_json_generated.cpp` | Round-trip all generated types |
| `tests/test_dm_json_gen/` | Generator output goldens (`make test-gen`) |

## Related docs

- [control-protocol.md](control-protocol.md) — wire protocol
- [frontend.md](frontend.md) — WebSocket client
- [control-protocol.md](control-protocol.md) — control socket overview

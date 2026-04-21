# dm-json migration — progress

This tracks the replacement of **nlohmann/json** with the **dm-json** stack: handwritten runtime (`include/rtpmidid/dm_json/runtime.hpp`, `lib/dm_json/runtime.cpp`), Python generator (`scripts/dm_json_gen.py`), CMake helper (`cmake/DmJson.cmake`), annotated types (`src/dm_json_status.hpp`, `src/dm_json_rpc.hpp`), and generated `build/.../dmjson_gen/dm_json_generated.{hpp,cpp}`.

## Phase checklist

| Phase | Status |
|-------|--------|
| 1. Runtime + generator + CMake + generator tests | **Done** |
| 2. Typed status structs + peer `fill_peer_status_row` | **Done** |
| 3. `midirouter_t::status_rows()` + mDNS snapshot types | **Done** |
| 4. Typed RPC (`control_rpc_dispatch_line`, split `router.create.*`, object-only `connect`) | **Done** |
| 5. WebSocket path (`web_server.cpp`) on same RPC | **Done** |
| 6. Peer `control_peer_command` (typed, no `json_t`) | **Done** |
| 7. CLI + frontend wire alignment | **Done** |
| 8. Remove `third_party/nlohmann/`, `src/json.hpp`, `src/json_fwd.hpp` | **Done** |
| 9. C++ tests (`test_dm_json_runtime`, `test_dm_json_generated`) + `make test-gen` | **Done** |

## Wire protocol (breaking vs old nlohmann era)

- **`connect`**: params must be a JSON **object** `{ "hostname", "port"?, "name"? }`. The CLI still accepts legacy positional arguments; they are normalized before send.
- **`router.remove`**: params object `{ "peer_id": <uint> }` (no bare array).
- **`router.create`**: removed. Use **`router.create.list`** plus one of:
  - `router.create.local_rawmidi`
  - `router.create.network_rtpmidi_client`
  - `router.create.network_rtpmidi_listener`
  - `router.create.local_alsa_peer`
- **`router.create.list`**: result is `{ "schemas": { "<type>_t": { "field": "description", ... }, ... } }`.
- **`status`**: `result` is a typed daemon status object (dm-json), not ad-hoc `nlohmann::json`.

## Key files

| Area | Files |
|------|--------|
| Runtime | `include/rtpmidid/dm_json/runtime.hpp`, `lib/dm_json/runtime.cpp` |
| Generator | `scripts/dm_json_gen.py` |
| CMake | `cmake/DmJson.cmake`, `src/CMakeLists.txt` (`rtpmidid-dmjson`, `PUBLIC` link) |
| Types | `src/dm_json_status.hpp`, `src/dm_json_rpc.hpp` |
| RPC | `src/control_rpc.{hpp,cpp}`, `src/control_socket.cpp` |
| Web | `src/web_server.cpp`, `frontend/src/app.tsx`, `frontend/src/rpc.ts` |
| CLI | `cli/rtpmidid-cli.py` |
| Tests | `tests/test_dm_json_runtime.cpp`, `tests/test_dm_json_generated.cpp`, `tests/test_dm_json_gen/` |

## Generator fixes (2026)

- **`from_json` object/array loops**: after each value, use `if (r.try_consume_object_end()) break; else expect_comma();` (and the array analogue) so single-field objects / one-element containers do not spin past the closing `}` / `]`.
- **Struct field parsing**: skip `//` / `/* */` at template depth 0 between fields; merge trailing same-line `// [dm-json: …]` after `;` into the field’s `FieldOpt`.
- **`omit_if_empty` vectors in `to_json`**: emit the full vector type through `emit_write_value(t, …)`, not the inner element type only.

## Regenerating goldens (generator output changed)

If `scripts/dm_json_gen.py` output changes intentionally, refresh unittest goldens:

```bash
python3 scripts/dm_json_gen.py \
  --out-dir /tmp/regen --header dm_json_generated.hpp --source dm_json_generated.cpp \
  tests/test_dm_json_gen/fixtures/minimal.hpp
cp /tmp/regen/dm_json_generated.hpp tests/test_dm_json_gen/expected/
cp /tmp/regen/dm_json_generated.cpp tests/test_dm_json_gen/expected/
```

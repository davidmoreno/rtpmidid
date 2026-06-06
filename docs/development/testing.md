# Testing and development

## Test structure

Tests live in `tests/` using the custom framework [`test_case.hpp`](../../tests/test_case.hpp).

```
tests/
├── test_midinormalizer.cpp
├── test_midirouter.cpp
├── test_midirouter2.cpp
├── test_rtpclient.cpp
├── test_rtppeer.cpp
├── test_rtpserver.cpp
├── test_settings.cpp
├── test_device_identity.cpp
├── test_device_query.cpp
├── test_device_registry.cpp
├── test_connection_db.cpp
├── test_connection_alsa_direct.cpp
├── test_alsa_monitor_tap.cpp
├── test_dm_json_runtime.cpp
├── test_dm_json_generated.cpp
├── test_dm_json_gen/          # generator goldens
└── test_utils.cpp
```

Wishlist and manual scenarios: [`tests/README.md`](../../tests/README.md).

## Running tests

```bash
make test                              # all C++ tests
make test-gen                          # dm_json_gen.py goldens
./build/tests/test_rtppeer             # single binary
```

Build workflow: [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Test-driven development

When adding features:

1. Write the failing test first
2. Run to confirm it fails
3. Implement minimum code to pass
4. Refactor while green
5. Add edge-case tests

Example new peer test:

```cpp
TEST_CASE("new_peer can send MIDI") {
    auto router = std::make_shared<midirouter_t>();
    auto peer = make_new_peer("test");
    router->add_peer(peer);
    mididata_t data = create_note_on(60, 127);
    peer->send_midi(peer->peer_id, data);
    REQUIRE(peer->last_sent == data);
}
```

## Test utilities

[`tests/test_utils.hpp`](../../tests/test_utils.hpp):

```cpp
void wait_for(std::chrono::milliseconds timeout);
mididata_t create_note_on(uint8_t note, uint8_t velocity);
```

Fake peer helper: `tests/test_fake_peer.hpp`.

## Areas needing more tests

- Journal N and other journal types
- mDNS discovery edge cases
- Connection timeout / CK synchronization
- Multiple simultaneous connections
- Reconnection after disconnect
- Control socket command coverage

## Debugging

```bash
# Debug logging
./rtpmidid --ini config.ini 2>&1 | grep -E "(DEBUG|INFO|WARNING|ERROR)"

# Network capture for bug reports
make capture PORT=5004

# GDB / Valgrind
make run-gdb
make run-valgrind
valgrind --suppressions=scripts/valgrind.supp ./rtpmidid
```

## Related docs

- [CONTRIBUTING.md](../../CONTRIBUTING.md) — build targets
- [development-notes.md](../development/development-notes.md) — how to add peers and commands
- [dm-json.md](../development/dm-json.md) — JSON test goldens

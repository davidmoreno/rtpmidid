# CONTRIBUTING

If you want to contribute, please create a pull request or issue at
https://github.com/davidmoreno/rtpmidid

Create a pull request if you have some code to show, even if it's not ready to
merge, so we can discuss it, and improve it.

If you have some idea you want to comment, but no code to show, create an
issue, and we can later create a related pull request if needed.

Please note that when contributing code you agree that this code is GPLv3
licensed.

## Build and development

### Prerequisites

- CMake, Ninja (or Make), C++17 compiler
- ALSA development libraries
- Avahi development libraries
- SQLite3 (for database features)
- Python 3 (dm-json code generator)
- Node.js (frontend build)

### Quick start

```bash
make build          # Release build → build/bin/rtpmidid
make build-dev      # Debug build
make run            # Build frontend + daemon, run with default.ini
make test           # Run all C++ tests
make test-gen       # Generator golden tests (dm_json_gen.py)
make help           # List all targets
```

### Key Makefile targets

| Target | Purpose |
|--------|---------|
| `build` / `build-dev` | Compile daemon and tests |
| `run` | Build and run development server |
| `test` | All C++ unit/integration tests |
| `test-gen` | Python unittest for `dm_json_gen.py` |
| `capture` | tcpdump network capture (`PORT=5004`) |
| `run-gdb` / `run-valgrind` | Debug runs for bug reports |
| `statemachines` | Regenerate rtpclient state machine from `lib/STATEMACHINES.md` |
| `docker-deb` / `docker-rpm` | Package builds in Docker |
| `install` | Install to `PREFIX` (default `/usr/local`) |

### CMake options

| Option | Default | Effect |
|--------|---------|--------|
| `RTPMIDID_ENABLE_TIMING` | OFF | Per-packet `steady_clock` timestamps on MIDI hot path |
| `ENABLE_TESTS` | ON | Build test binaries |
| `ENABLE_PCH` | varies | Precompiled headers (off in parallel builds) |

Example:

```bash
cd build && cmake .. -DRTPMIDID_ENABLE_TIMING=ON && ninja
```

### Frontend

```bash
./packaging/build-frontend.sh   # → frontend/dist/
```

The daemon serves `frontend/dist` when `[web] root=frontend/dist` in INI.
See [docs/development/frontend.md](docs/development/frontend.md).

### Development docs

Start with [AGENTS.md](AGENTS.md) and [docs/README.md](docs/README.md). Doc
maintenance policy is in AGENTS.md — update matching docs with each PR.

Architecture: [docs/architecture/](docs/architecture/). Development:
[docs/development/](docs/development/).

### Test-driven development

When adding features, prefer writing a failing test first. Tests live in
`tests/` using `test_case.hpp`. See [docs/development/testing.md](docs/development/testing.md) and
[tests/README.md](tests/README.md).

```bash
make test
./build/tests/test_device_identity   # single test binary
```

### Debugging

```bash
make capture PORT=5004     # attach to bug reports
make run-gdb               # backtrace on crash
# Valgrind suppressions: scripts/valgrind.supp
```

## CONTRIBUTORS

* David Moreno - https://github.com/davidmoreno - Creator
* Albert Graef - https://github.com/agraef - SysEx support, added more MIDI messages

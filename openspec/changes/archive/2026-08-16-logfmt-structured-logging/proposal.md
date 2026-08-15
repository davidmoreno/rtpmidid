## Why

Log lines are human sentences with positional `{}` placeholders (e.g. `New ALSA connection {} from port {}:{} -> {}:{}`), which read fine but are painful to grep, parse, or pipeline through tools like `jq`/`lnav`/journald. Structured, logfmt-style `key=value` output makes every log line machine-parseable and greppable by field, and color-highlighting the keys makes the raw stream far easier to scan by eye.

## What Changes

- **Producer side** — all log call sites move to `key=value` formatting. A new `quoted_t{...}` helper quotes/escapes values that contain spaces, `=`, or `"` (and escapes control chars), so `INFO("value={}", quoted_t{value})` renders correctly.
- **Consumer side** — each line gains the built-in fields `level=`, `thread=`, and `filename=<basename>:<lineno>`. **BREAKING**: the `log_message_t` shape splits the combined `origin` string into separate `file` + `lineno`.
- **Render side** — the rendered format changes from the padded `[LEVEL] [tag] origin | body` to logfmt `key=value` pairs. **BREAKING**: existing log line format is replaced.
- **Color highlighting** — the renderer colors the `level=` token by severity (the old per-level colors: DEBUG blue, INFO none, WARNING yellow, ERROR red), colors the built-in keys (thread/filename) blue, and all producer keys yellow, using a small quote-aware tokenizer. The built-in prefix is padded so the producer's message starts at a fixed column. Color is gated by a process-global bool computed once at startup.
- **Color configuration** — disable via `NO_COLOR` or `--log-no-color`; force via `FORCE_COLOR`; configure via a new INI `log_color` setting (`never`/`always`/`auto`); default is TTY auto-detection.

## Capabilities

### New Capabilities
- `logfmt-structured-logging`: producer key=value formatting with the `quoted_t` helper, consumer-supplied built-in fields, logfmt line rendering with color highlighting, and the color enable/disable configuration (env vars, CLI flag, INI setting, TTY detection).

### Modified Capabilities
- `thread-named-logging`: its "Log line rendering with tag" requirement pins the `[LEVEL] [tag] origin` padded-40 format; this is replaced by the logfmt `thread=` field (the tag capture, owned-copy, and thread-naming requirements are unchanged).

## Impact

- **Code**: all 342 log call sites across `src/`, `lib/`, `include/`; `include/rtpmidid/logger.hpp`, `lib/logger.cpp`, `include/rtpmidid/formatterhelper.hpp`, `src/logger_actor.cpp`, `src/main.cpp`, `src/argv.cpp`, `src/settings.hpp`.
- **Config**: new `log_color` INI setting; new `--log-no-color` CLI flag; `NO_COLOR`/`FORCE_COLOR` env vars.
- **Tests**: `tests/test_logger.cpp` (exact-byte assertions on the old format), `tests/test_actor.cpp` (captures `log_message_t`), plus new tests for `quoted_t`, the tokenizer/colorizer, and color config precedence.
- **No external dependencies** added; libfmt/`std::format` formatter machinery already in place.

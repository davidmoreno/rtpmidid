## Context

Logging flows through the lib's `logger_t::log()` (include/rtpmidid/logger.hpp): the producing thread filters by level, formats the body into a per-thread buffer, and hands a `log_message_t{level, origin, text, thread_name}` to a global sink installed by the logger actor — the single thread that renders lines via `logger_format_line()` (lib/logger.cpp). Today's render format is `[LEVEL] [tag] origin` padded to 40 columns, then ` | body`, with level-based ANSI color (DEBUG blue, WARNING yellow, ERROR red, INFO none) always emitted. The level enum's formatter emits `"INFO "`/`"WARN "` (trailing space) to make `[INFO ]`/`[WARN ]` align. `origin` is a pre-combined `"file.cpp:42"` string built by `log_origin()`.

There are 342 log call sites, most human sentences with positional `{}` placeholders. The daemon CLI is C++ (src/main.cpp, src/argv.cpp); the separate `cli/rtpmidid-cli.py` is a Python control-socket client and is out of scope. `NO_COLOR`/`isatty` appear nowhere in the tree today.

## Goals / Non-Goals

**Goals:**
- logfmt-style `key=value` output at every call site, with a `quoted_t{...}` helper that quotes/escapes values needing it.
- Built-in fields `level=`, `thread=`, `filename=<basename>:<lineno>` prepended by the consumer.
- Color highlighting: the `level=` token by severity (old per-level colors), built-in keys (thread/filename) blue, producer keys yellow, with the built-in prefix padded so the producer's message aligns.
- Color gated by a process-global bool computed once at startup: `NO_COLOR`, `FORCE_COLOR`, `--log-no-color`, INI `log_color`, and TTY auto-detect.
- Cheap rendering: skip parsing/coloring entirely when color is off.

**Non-Goals:**
- Changing the Python CLI's logging (separate codebase, talks over the control socket).
- Structured (typed) key/value transport across the sink — the producer still formats to text; the renderer tokenizes for coloring only.
- A `--log-color` CLI flag (force-on via CLI); `FORCE_COLOR` covers force-on today.
- Per-field ANSI styling beyond key color (values stay default-colored).

## Decisions

### D1: Full logfmt — every message is `key=value`
Every call site keeps its human-readable message as bare text (logfmt fragments) and writes the structured data as `key=value` pairs. Alternatives rejected: (a) a `msg="..."` wrapper around every message — more quoting work and the prose is a compile-time literal that needs no runtime processing; (b) leaving data as bare positional `{}` — not parseable by field. Naming convention: snake_case keys (e.g. `connection`, `from_port`, `reason`).

### D2: `quoted_t{...}` stores a `std::string_view`
A wrapper type in include/rtpmidid/formatterhelper.hpp with a `FMT::formatter<quoted_t>` specialization. It stores `std::string_view` (zero copy) and is only ever consumed as an immediate `FMT::format_to_n` argument, so the referenced value is alive for the duration of the call. A value is quoted when it contains a space, `=`, or `"`; inside quotes, `\` → `\\` and `"` → `\"`; control chars `\n`/`\t` are escaped to literal backslash-n/t to preserve the one-line invariant. Empty value renders as `key=` (logfmt-valid). Alternative rejected: storing `std::string` (always-safe but copies per call); the immediate-arg pattern makes `string_view` safe and allocation-free.

### D3: `log_message_t` splits `origin` into `file` + `lineno`
`log_message_t` gains `std::string file` (basename, from `log_origin`) and `int lineno`, replacing the combined `origin` string. This is a **BREAKING** shape change; the renderer re-joins them as a single `filename=<basename>:<lineno>` field. `to_string(log_message_t)` (mailbox-drop diagnostics) is updated to the new fields.

### D4: `level=warning`, lowercase, no padding
A dedicated `const char *log_level_name(level)` returns `debug`/`info`/`warning`/`error` (lowercase; `warning` not `warn`, matching existing config/`to_text`). The renderer uses it. The `ENUM_FORMATTER` for `logger_level_t` drops its trailing-space padding hack (so `"INFO "`→`"INFO"`); note this formatter is shared with the settings dump, so `DEBUG("settings after argument parsing: {}", *settings)` output changes cosmetically.

### D5: `thread=` always emitted
All four built-in keys are always emitted, so every line has a uniform, machine-parseable shape. An untagged thread renders `thread=` (empty value, valid logfmt). Alternative rejected: omitting `thread=` when empty — loses uniform key order for `grep`/parsers.

### D6: Quote-aware mini tokenizer, not regex
The renderer colors keys with a small single-pass tokenizer that toggles `in_quote` on `"` and treats `identifier=` at token start as a key. This correctly ignores an `=` inside a quoted value (e.g. `name="a=b"`). Alternative rejected: a regex for keys — naively matches `=` inside quoted values; a quote-aware regex is effectively the tokenizer anyway.

### D7: Color classification is structural, not a whitelist
The renderer colors the `level=` token by severity (DEBUG blue, INFO uncolored, WARNING yellow, ERROR red) and the two built-in keys (`thread`, `filename`) blue; every other key is producer-supplied and colored yellow. No catalog of custom keys to maintain.

### D8: The built-in prefix is padded to align the producer's message
The `level= thread= filename=` prefix is padded to a fixed column (64) so the producer's message — the yellow keys — starts at the same column on every line. Longer prefixes overflow without truncation (best-effort alignment).

### D9: Color config — precedence and one-shot evaluation
A process-global `std::atomic<bool>` (or a set-once static) holds `log_color_enabled`. It is computed once at startup from, in precedence order: (1) `--log-no-color` → off; (2) `NO_COLOR` present and non-empty → off (absolute off, per no-color.org, beats even `FORCE_COLOR`); (3) `FORCE_COLOR` present and non-empty → on; (4) INI `log_color` = `never`/`always`/`auto`; (5) default `isatty(STDOUT_FILENO)`. When off, `logger_format_line` returns the raw line and never tokenizes. `FORCE_COLOR` overrides INI `never` (standard CLI > env > config > default precedence).

### D10: Placement — helper in lib, config in daemon
`quoted_t` and the colorizer/tokenizer live in the lib (formatterhelper.hpp / logger.cpp) so the direct-print fallback and the logger actor share them byte-for-byte. The color bool and its setter live in the lib (lib/logger.cpp); the daemon computes it at startup (src/main.cpp) from argv (src/argv.cpp `--log-no-color`), env, and the new `settings_t::log_color` INI field (src/settings.hpp + jsondm ini converter).

## Risks / Trade-offs

- [342-site migration is mechanical but judgment-heavy; keys will drift in naming/style] → snake_case convention (D1) plus a grep-driven review pass for outliers.
- [Exact-byte tests break] → rewrite tests/test_logger.cpp; add a test hook to force color on/off so the colorizer is testable in non-tty CI.
- [A value containing a raw newline would break the one-line invariant] → `quoted_t` escapes `\n`/`\t` (D2).
- [Per-line tokenizer cost in high-volume logs] → tokenizer runs only when color is enabled; production/systemd (no tty, no force) skips it entirely (D8).
- [Changing the shared level formatter alters the settings dump] → cosmetic only; accepted.
- [Lib consumers/tests log without a tag] → `thread=` empty is valid and uniform (D5).

## Migration Plan

Single-daemon change, no rolling deploy. Phases in the task list: (1) add `quoted_t` + `log_level_name` + `log_message_t` split + tokenizer/colorizer in the lib; (2) rewrite `logger_format_line` to logfmt and wire the color bool; (3) add `settings_t::log_color`, `--log-no-color`, env/tty detection in the daemon; (4) migrate all call sites; (5) update/add tests. Rollback is a revert of the change; no persisted format depends on the old line text.

## Open Questions

None — producer format, quoting rules, built-in fields, color scheme, and config precedence are all resolved above.

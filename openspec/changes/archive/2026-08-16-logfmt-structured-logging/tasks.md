## 1. Producer helper (lib)

- [x] 1.1 Add `quoted_t` (holding `std::string_view`) and its `FMT::formatter<quoted_t>` specialization in include/rtpmidid/formatterhelper.hpp, with the quoting/escaping rules from the spec (quote on space/`=`/`"`; escape `\`→`\\`, `"`→`\"`, `\n`, `\t`; empty → bare `key=`).
- [x] 1.2 Add `log_level_name(level)` returning lowercase `debug`/`info`/`warning`/`error`; remove the trailing-space padding from the `ENUM_FORMATTER` for `logger_level_t`.

## 2. Message shape and renderer (lib)

- [x] 2.1 Split `log_message_t::origin` into `std::string file` (basename) and `int lineno`; update `log_origin()` and `logger_t::log()` to populate them.
- [x] 2.2 Update `to_string(const log_message_t&)` to the new fields.
- [x] 2.3 Add the process-global color flag (`set_log_color_enabled` / `log_color_enabled`) in lib/logger.cpp.
- [x] 2.4 Implement the quote-aware key tokenizer/colorizer (built-in keys blue, all other keys yellow; `=` inside quotes is not a key).
- [x] 2.5 Rewrite `logger_format_line` to emit `level= thread= file= lineno= <body>`; tokenize/color only when the color flag is on.

## 3. Configuration (daemon)

- [x] 3.1 Add `settings_t::log_color` (enum `never`/`always`/`auto`, default `auto`) with its jsondm ini converter in src/settings.hpp.
- [x] 3.2 Add the `--log-no-color` CLI flag in src/argv.cpp.
- [x] 3.3 Compute the color flag once at startup in src/main.cpp, applying precedence: `--log-no-color` → off; `NO_COLOR` non-empty → off; `FORCE_COLOR` non-empty → on; INI `log_color`; else `isatty(STDOUT_FILENO)`.

## 4. Call-site migration (full logfmt)

- [x] 4.1 Migrate all `src/` call sites to `key=value` with `quoted_t` on string values and a `msg=` summary on every message (snake_case keys).
- [x] 4.2 Migrate all `lib/` call sites the same way.

## 5. Tests

- [x] 5.1 Rewrite tests/test_logger.cpp exact-byte assertions for the logfmt format and new `log_message_t` shape.
- [x] 5.2 Update tests/test_actor.cpp `log_message_t` field access (`origin` → `file`/`lineno`).
- [x] 5.3 Add tests for `quoted_t` (safe, spaces, embedded quote, empty, control chars).
- [x] 5.4 Add tests for the colorizer with color forced on (built-in blue vs custom yellow; `=` inside quotes).
- [x] 5.5 Add tests for color config precedence (NO_COLOR, FORCE_COLOR, `--log-no-color`, INI, non-tty default).

## 6. Verification

- [x] 6.1 Build and run the full test suite (`make test` or equivalent).
- [x] 6.2 Smoke-test the daemon: piped output (no color), TTY, `--log-no-color`, `NO_COLOR=1`, `FORCE_COLOR=1`, and a sample `log_color` INI value.

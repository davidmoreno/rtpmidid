# Logging system

Compile-time and runtime configurable logging with colorized output.

Files: [`include/rtpmidid/logger.hpp`](../../include/rtpmidid/logger.hpp),
[`lib/logger.cpp`](../../lib/logger.cpp).

Dedicated **logger thread** drains a lock-free queue (see [event-loop.md](../architecture/event-loop.md)).

## Log levels

| Level | Macro | Color | Description |
|-------|-------|-------|-------------|
| DEBUG | `DEBUG(...)` | Blue | Detailed debugging |
| INFO | `INFO(...)` | None | Operational messages |
| WARNING | `WARNING(...)` | Yellow | Warning conditions |
| ERROR | `ERROR(...)` | Red | Error conditions |

## Compile-time filtering

`LOG_LEVEL` preprocessor define (1=DEBUG … 4=ERROR). Statements below the
threshold compile out entirely:

```cpp
#define LOG_LEVEL 2  // Compiles out DEBUG
```

## Runtime filtering

```cpp
rtpmidid::logger2.set_log_level(rtpmidid::logger_level_t::WARNING);
```

INI:

```ini
[general]
log_level=info
```

Accepts `debug`/`info`/`warning`/`error` (case-insensitive) or `0`–`3`.

## Special macros

```cpp
WARNING_RATE_LIMIT(10, "High frequency event: {}", count);
ERROR_ONCE("Configuration issue detected");
WARNING_ONCE("Deprecated feature used");
```

Use rate-limited / one-shot macros on hot paths. See [performance.md](../architecture/performance.md).

## Log format

```
[DEBUG] poller.cpp:42        | Added timer 5. 60.0 s (3 pending)
[INFO ] main.cpp:171         | Waiting for connections.
[WARN ] rtppeer.cpp:89       | Connection timeout for peer 3
[ERROR] aseq.cpp:156         | Failed to open ALSA sequencer
```

## Related docs

- [performance.md](../architecture/performance.md) — avoid logging on MIDI hot path
- [configuration.md](configuration.md) — `log_level` INI key

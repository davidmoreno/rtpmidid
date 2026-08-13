## Why

With the actor architecture (add-actor-architecture), every actor runs on its
own thread — peers, control connections, router, supervisor, mdns, alsa,
worker, logger — yet log lines do not say which actor produced them, and
htop/top/gdb show opaque thread names. As concurrency grows (per-peer and
per-control-connection threads), "who logged what" and "which thread is doing
what" are hard to answer. Both costs are cheap to fix at existing choke points:
all actor threads funnel through `actor_t::thread_main()`, and all log calls
funnel through `logger_t::log()`.

## What Changes

- **Per-thread log tag**: a `thread_local` tag (the actor name) is set when an
  actor thread starts, and also on the main thread and the reaper thread. Every
  `log_message_t` captures the producing thread's tag at production time (the
  producer and the renderer are different threads — the logger actor prints
  what others logged). The line format becomes
  `[INFO ] [router] file.cpp:12 | msg` with the tag inside the existing
  40-column padded prefix. An empty tag renders the exact current output, so
  lib-only programs, early-startup output, tests, and the direct-print fallback
  stay byte-identical.
- **OS thread names**: actor threads and the reaper thread get a
  `pthread_setname_np` name of the form `rtpmidid:<name>`, visible in
  htop/top -H/ps -eLf/gdb/perf. The kernel caps comm at 15 chars; truncation is
  silent and accepted (the full name always remains in the log tag).
- **Main thread exception**: the main thread gets the log tag `main` only — its
  comm is NOT renamed, because the main thread's comm IS the process name
  (`killall`, `pgrep -x`, and systemd depend on the process staying `rtpmidid`).
- **Allocation discipline**: the tag registry is set once per thread at
  construction (allocation fine). The per-message copy is a `std::string` whose
  SSO holds typical actor names without heap allocation; `log_message_t` already
  allocates per call (origin + text), and the MIDI hot path never logs per
  message by design.

## Capabilities

### New Capabilities

- `thread-named-logging`: per-thread log context (a thread-local tag captured
  into every log message and rendered in the line prefix) and OS-level thread
  naming (`rtpmidid:<name>` comm for actor and reaper threads, main excluded).

### Modified Capabilities

- None. (`actor-runtime` exists only as an unarchived delta of
  add-actor-architecture and is not yet in main specs; the naming behavior is
  specified here instead.)

## Impact

- `include/rtpmidid/logger.hpp` + `lib/logger.cpp`: thread tag registry
  (setter/getter, `thread_local`), `thread_name` field on `log_message_t`,
  tag rendering in `logger_format_line`.
- `src/actor.hpp`: `thread_main()` sets the tag and the comm name for every
  actor thread.
- `src/supervisor_actor.cpp`: reaper thread sets tag + comm.
- `src/main.cpp`: main thread sets the `main` tag (no comm change).
- `tests/test_logger.cpp`: add a tag-rendering case; the existing exact-format
  assertion is unchanged and must keep passing.
- No dependencies, no ABI concerns (internal lib, same process).

## Context

rtpmidid runs an actor architecture (add-actor-architecture): every actor owns
one `std::jthread`, one private poller, and one mailbox; all actor threads
funnel through `actor_t::thread_main()` (src/actor.hpp), which already applies
per-thread scheduling (`apply_scheduling()`). Logging goes through
`logger_t::log()` in the lib (include/rtpmidid/logger.hpp): the producing
thread formats the body into a per-thread buffer (thread_local, no allocation)
and hands a `log_message_t{level, origin, text}` to a global sink
(`logger_log_sink`) installed by the logger actor, which is the single thread
that renders lines via `logger_format_line()`. The render format is
`[INFO ] file.cpp:12 | msg` with the prefix padded to 40 columns, and the
producer and the renderer are different threads.

Non-actor threads: the main thread and the supervisor's reaper thread
(src/supervisor_actor.cpp, plain `std::thread`). No thread naming exists today
(no `pthread_setname_np` anywhere).

## Goals / Non-Goals

**Goals:**
- Every log line produced by an actor thread identifies that actor.
- Actor and reaper threads appear as `rtpmidid:<name>` in htop/top -H/ps -eLf/gdb/perf.
- The main thread is tagged `main` in logs; the process comm stays `rtpmidid`.
- Zero behavior change when no tag is set (lib-only programs, tests, early startup).
- No allocation on the MIDI hot path; per-call cost bounded by SSO for typical names.

**Non-Goals:**
- Renaming the main thread's comm (it IS the process name; `killall`/`pgrep -x`/systemd depend on it).
- Naming library-spawned threads (avahi/ALSA internals) — out of our control.
- Thread-name lookups by other processes (no registry/gettid map, no /proc queries).
- Per-message attribution of *message subjects* — the tag identifies the calling thread, not necessarily the message topic (e.g. the supervisor logs "peer-3 died" tagged `[supervisor]`).
- Actor-id disambiguation in tags (name-only; ids remain available in status APIs).

## Decisions

### D1: The tag registry lives in the lib, not the actor runtime
The `thread_local` tag (setter/getter, e.g. `set_log_thread_tag()`) lives in
include/rtpmidid/logger.hpp next to the existing `thread_buffer()` thread_local,
because `log()` must read it at production time and layering is src→lib (the
actor runtime cannot be seen by the lib). The actor runtime merely calls the
setter. The lib's direct-print fallback and the logger actor share the same
rendering, so both paths get the tag for free.

### D2: The tag is captured into the message at production time, as an owned copy
`log_message_t` gains a `std::string thread_name`. The producer copies its
thread_local tag into the message. Alternatives considered and rejected:
- `const char*` into the thread_local — the producing thread can exit before the
  logger drains its bounded mailbox, destroying the thread_local → dangling
  pointer. Never store pointers into thread-locals in cross-thread messages.
- Render-time lookup by thread id — requires a registry with lifecycle
  management; overkill for a debug aid.

### D3: `std::string` field; SSO is the allocation guard
The per-call copy is the only per-call cost of this feature. It is acceptable
because `log_message_t` already allocates per call (origin and text are heap
strings), typical actor names ("router", "supervisor", "control-conn-5") fit in
SSO (≤15 chars on libstdc++) and allocate nothing, and the MIDI hot path never
logs per message by design (drop warnings are rate-limited; connect/disconnect
are per-event). Strict zero-heap per call (a fixed `char[N]` in the message)
remains a fallback if profiling ever demands it.

### D4: comm format `rtpmidid:<name>`, 15-char cap, explicit truncation
Chosen over the originally requested `rtpmidid [<name>]` after the arithmetic:
brackets + spaces cost 11 of the 15 TASK_COMM_LEN chars, leaving 4 for the name
("supervisor" → "rtpmidid [supe") and making `control-conn-5`/`control-conn-6`
indistinguishable. The colon form leaves up to 6 name chars, is greppable in
`ps -eLf`, and still truncates long names — but the full name always remains in
the log tag, which is the precise disambiguator. Truncation is explicit in the
caller: glibc's `pthread_setname_np` returns ERANGE for names ≥16 chars (it
does NOT silently truncate — only the raw `prctl(PR_SET_NAME)` would), so the
comm is clamped to 15 chars before the call. Alternatives considered:
bare name (maximum fidelity, but ambiguous across processes in `ps -eLf`) and
space separator (identical budget, marginally nicer to read).

### D5: Main thread — tag only, never rename the comm
The main thread's comm is the process comm. `prctl(PR_SET_NAME)`/`pthread_setname_np`
on the main thread would rename the process as seen by `top`, `killall`,
`pgrep -x`, and systemd. The main thread sets the log tag `main` and nothing else.

### D6: Tag rendered inside the existing 40-column padded prefix
`logger_format_line` builds `[LEVEL] [tag] origin`, pads the combined prefix to
40 columns, then ` | body`. Empty tag → exactly today's bytes
(`[LEVEL] origin`), so tests/test_logger.cpp's exact-format assertion and all
lib-only output are untouched.

### D7: Empty actor names produce no tag and no comm
Peer factories can create actors with an empty name
(control_socket_actor.cpp uses `p.name.value_or("")`). Empty tag → omitted
(see D6); empty name → skip `pthread_setname_np` (never write a bare
`rtpmidid:`).

### D8: Hook points
- `actor_t::thread_main()`: after `apply_scheduling()`, set tag =
  `config_.name`, and if non-empty, comm = `"rtpmidid:" + config_.name`
  (formatted once per thread at start — allocation fine, off the hot path).
- `supervisor_actor_t::on_start()`: the reaper lambda sets tag `reaper` and
  comm `rtpmidid:reaper` (13 chars, fits exactly).
- `main()`: set tag `main` early, before the first log call that must carry it.

## Risks / Trade-offs

- [Truncated comms collide: `control-conn-5` and `control-conn-6` both show `rtpmidid:contro` in htop] → Accepted; the full name is in the log tag. Documented in the proposal.
- [Renaming the main thread's comm would break `killall`/`pgrep -x`/systemd process identity] → Mitigated by design (D5): main is tag-only.
- [Dangling pointer if the tag were passed by reference into the message] → Mitigated by design (D2): owned copy.
- [Log format change breaks output-compat tests] → Mitigated by design (D6): empty-tag path is byte-identical; a new tagged test case covers the new format.
- [Names with spaces/`%` (mdns entries like `Piano#1234 - 192.168.1.5:5004`) look odd or escaped in top] → Cosmetic; truncated anyway by the 15-char cap; accepted.
- [Library-spawned threads (avahi/ALSA) remain unnamed] → Out of scope; documented non-goal.

## Migration Plan

Internal, single-process change with no external contract: deploy by building
the daemon. The empty-tag default keeps every other consumer (lib users, tests)
byte-identical, so rollback is a revert with no compatibility window. No config
flags, no versioned API, no data migration.

## Open Questions

None blocking. Future possibilities (not decided here): including the actor id
in the tag when names collide; a `current_log_thread_tag()` getter exposed for
status/diagnostics; folding the spawn-side naming into `actor-runtime` once that
delta is archived to main specs.

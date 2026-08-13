## 1. Library: thread tag registry and message field

- [x] 1.1 Add a `thread_local` tag registry to include/rtpmidid/logger.hpp (setter/getter, e.g. `set_log_thread_tag(const std::string&)` / `current_log_thread_tag()`), adjacent to the existing `thread_buffer()` thread_local; implement in lib/logger.cpp
- [x] 1.2 Add `std::string thread_name` to `log_message_t` (include/rtpmidid/logger.hpp)
- [x] 1.3 In `logger_t::log()`, capture the producing thread's tag into `msg.thread_name` after the level filter (only when the message is actually emitted), before handing off to the sink
- [x] 1.4 Extend `logger_format_line()` (lib/logger.cpp) to render `[LEVEL] [tag] origin` padded to 40 columns when the tag is non-empty; keep the empty-tag path byte-identical to today

## 2. Thread naming hooks

- [x] 2.1 In `actor_t::thread_main()` (src/actor.hpp), after `apply_scheduling()`: set the log tag from `config_.name`; if the name is non-empty, set the comm to `rtpmidid:<name>` via `pthread_setname_np` (formatted once per thread at start)
- [x] 2.2 In `supervisor_actor_t::on_start()` (src/supervisor_actor.cpp), set the reaper thread's log tag to `reaper` and comm to `rtpmidid:reaper` inside the reaper lambda
- [x] 2.3 In `main()` (src/main.cpp), set the main thread's log tag to `main` before the first log call that must carry it; do NOT change the main thread's comm

## 3. Tests

- [x] 3.1 Add a tagged rendering case to tests/test_logger.cpp (e.g. tag `router` renders `[INFO ] [router]        file.cpp:12 | msg`); verify the existing untagged exact-format assertion still passes unchanged
- [x] 3.2 Add a test that a thread's log tag is captured into messages produced by that thread (set tag, log, render) and that an empty tag renders the legacy format

## 4. Build and verification

- [x] 4.1 Build the daemon and run the test suite (`make` / ctest), confirming no regressions
- [x] 4.2 Manual smoke check: run the daemon, confirm log lines carry `[actor]` tags, and confirm htop/top -H (`ps -eLf`) shows `rtpmidid:<name>` comms for actor and reaper threads while the process comm stays `rtpmidid`

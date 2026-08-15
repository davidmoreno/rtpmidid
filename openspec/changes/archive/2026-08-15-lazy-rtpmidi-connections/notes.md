# Implementation notes — lazy-rtpmidi-connections

## Parked: flaky teardown in threaded network tests

`tests/test_network_actor.cpp::test_connect_handshake_and_midi_loop` (and
occasionally its siblings) fails intermittently under load with either
`terminate called without an active exception` or an indefinite hang after
the test body finishes. Symptoms point at the teardown path: the base-class
`~actor_t` join races with derived-member destruction when the actor thread
has not fully exited, and the helper `stop_and_wait` (waiting on `stopped_t`
via the supervisor mailbox) does not cover every case (e.g. the worker actor
is created without a supervisor mailbox).

**For the time being these tests rely on their internal `wait_until`
timeouts; the binary-level hang comes from destructors joining stuck actor
threads. This needs a follow-up:**

- [ ] Reproduce with a captured `gdb -p` backtrace of the hung state
      (scripts at /tmp/hangcatch.sh + /tmp/gdbloop.sh patterns; ASan is
      unusable on this box: `/usr/lib64/libasan.so.8.0.0` is missing).
- [ ] Make actor teardown deterministic in tests: either a public
      "stopped" flag on `actor_base_t` or per-test supervisor mailboxes on
      every threaded actor (including the worker), so tests can join before
      dropping the shared_ptr.
- [ ] Consider ctest `TIMEOUT` properties on the network/ALSA integration
      tests so a hung teardown fails the suite instead of hanging CI.
- [ ] Investigate SCHED_FIFO promotion of router/peer actors in the test
      binaries (settings defaults: rt_enable=true) as a starvation
      candidate under parallel load.

The rest of the suite is green (21/21 in serial runs); the flake is
load/timing dependent.

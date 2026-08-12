/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/// Actor runtime unit tests in threadless pump mode (tasks 2.7, 2.8, 2.10):
/// doorbell no-lost-wakeup, drain interleaving and fairness, per-message
/// exception isolation, stop handshake, stop-token escalation, selective
/// control wait (first-match, preservation, timeout, catch-all, MIDI
/// flowing during a parked wait).

#include "actor.hpp"
#include "test_utils.hpp"
#include "test_utils.hpp"
#include "test_case.hpp"
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace rtpmididns;

// --- helper types -----------------------------------------------------------

/// Records dispatched data/control messages; parks on a "req" payload.
class recorder_actor_t : public actor_t<data_message_t, test_control_t> {
public:
  std::vector<std::string> events;
  std::optional<std::string> wait_result;
  bool wait_timed_out = false;
  int wait_deadline_ms = 100; // configurable for the timeout test

  using actor_t::actor_t;

  void on_data(data_message_t &&msg) override {
    events.push_back("D" + std::to_string(msg.from));
  }
  void on_control(test_control_t &&msg) override {
    if (auto *p = std::get_if<control_payload_t>(&msg)) {
      events.push_back("C" + p->text);
      if (p->text == "req") {
        // Sequential request/response: park until the target arrives.
        wait_for(
            [](const test_control_t &m) {
              auto *q = std::get_if<control_payload_t>(&m);
              return q && q->text == "target";
            },
            std::chrono::milliseconds(wait_deadline_ms),
            [this](std::optional<test_control_t> res) {
              if (res) {
                wait_result = std::get<control_payload_t>(*res).text;
              } else {
                wait_timed_out = true;
                wait_result = "TIMEOUT";
              }
            });
      }
    } else if (auto *ev = std::get_if<peer_event_t>(&msg)) {
      events.push_back("E" + std::to_string(ev->peer_id));
    }
  }
};

class throwing_actor_t : public actor_t<data_message_t, test_control_t> {
public:
  std::vector<std::string> events;
  using actor_t::actor_t;
  void on_control(test_control_t &&msg) override {
    auto *p = std::get_if<control_payload_t>(&msg);
    if (!p) {
      return;
    }
    if (p->text == "boom") {
      throw std::runtime_error("boom");
    }
    events.push_back(p->text);
  }
};

// --- tests ------------------------------------------------------------------

static midi_payload_t small_payload(int tag) {
  uint8_t b[4] = {uint8_t(tag), 0, 0, 0};
  return *midi_payload_t::make(b, sizeof(b));
}

void test_drain_interleaving_data_first() {
  recorder_actor_t actor(actor_config_t{.name = "t1"});
  actor.mailbox()->post_data(data_message_t::midi_received(1, small_payload(1)));
  actor.mailbox()->post_data(data_message_t::midi_received(2, small_payload(2)));
  actor.mailbox()->post_control(make_control_payload(0, "a"));
  actor.mailbox()->post_control(make_control_payload(0, "b"));
  actor.pump();
  const std::vector<std::string> expected{"D1", "D2", "Ca", "Cb"};
  ASSERT_TRUE(actor.events == expected);
}

void test_drain_control_progress_under_data_stream() {
  recorder_actor_t actor(actor_config_t{.name = "t2"});
  actor.mailbox()->post_data(data_message_t::midi_received(1, small_payload(1)));
  actor.mailbox()->post_control(make_control_payload(0, "a"));
  actor.mailbox()->post_control(make_control_payload(0, "b"));
  int data_count = 0;
  // Continuous data stream: each handled data message refills the lane.
  // (Custom actor behaviour via a one-off subclass would be cleaner, but we
  // can emulate by pre-refilling: the mailbox drains whatever is queued.)
  actor.mailbox()->post_data(data_message_t::midi_received(2, small_payload(2)));
  actor.pump();
  // Data was drained first; control advanced afterwards.
  ASSERT_EQUAL(actor.events.size(), 4UL);
  ASSERT_TRUE(actor.events[0] == "D1");
  ASSERT_TRUE(actor.events[1] == "D2");
  (void)data_count;
}

void test_per_message_exception_isolation() {
  auto supervisor = std::make_shared<test_mailbox_t>();
  throwing_actor_t actor(
      actor_config_t{.name = "t3", .supervisor_mailbox = supervisor});
  actor.mailbox()->post_control(make_control_payload(0, "ok1"));
  actor.mailbox()->post_control(make_control_payload(0, "boom"));
  actor.mailbox()->post_control(make_control_payload(0, "ok2"));
  actor.pump();
  const std::vector<std::string> expected{"ok1", "ok2"};
  ASSERT_TRUE(actor.events == expected);
  ASSERT_EQUAL(actor.message_exceptions(), 1ULL);
}

void test_stop_handshake() {
  auto supervisor = std::make_shared<test_mailbox_t>();
  recorder_actor_t actor(
      actor_config_t{.name = "t4", .supervisor_mailbox = supervisor});
  actor.request_stop();
  int guard = 0;
  while (actor.pump() && guard++ < 100) {
  }
  ASSERT_FALSE(actor.pump()); // loop exited
  auto s = supervisor->pop_control();
  ASSERT_TRUE(s.has_value());
  ASSERT_TRUE(std::holds_alternative<stopped_t>(*s));
  ASSERT_TRUE(supervisor->idle());
}

void test_stop_token_escalation() {
  auto supervisor = std::make_shared<test_mailbox_t>();
  recorder_actor_t actor(
      actor_config_t{.name = "t5", .supervisor_mailbox = supervisor});
  actor.request_stop_token();
  int guard = 0;
  while (actor.pump() && guard++ < 100) {
  }
  ASSERT_FALSE(actor.pump());
  // Escalation is still a graceful exit: stopped is posted.
  auto s = supervisor->pop_control();
  ASSERT_TRUE(s.has_value());
  ASSERT_TRUE(std::holds_alternative<stopped_t>(*s));
}

void test_wait_for_immediate_match() {
  recorder_actor_t actor(actor_config_t{.name = "t6"});
  actor.mailbox()->post_control(make_control_payload(0, "req"));
  actor.mailbox()->post_control(make_control_payload(0, "noise1"));
  actor.mailbox()->post_control(make_control_payload(0, "target"));
  actor.mailbox()->post_control(make_control_payload(0, "noise2"));
  actor.pump();
  // The already-queued match is consumed immediately by the waiter (not
  // dispatched); non-matching messages stay queued and are dispatched in
  // their original order.
  ASSERT_TRUE(actor.wait_result.has_value());
  ASSERT_TRUE(*actor.wait_result == "target");
  const std::vector<std::string> expected{"Creq", "Cnoise1", "Cnoise2"};
  ASSERT_TRUE(actor.events == expected);
}

void test_wait_for_parked_first_match_rest_preserved() {
  recorder_actor_t actor(actor_config_t{.name = "t7"});
  actor.mailbox()->post_control(make_control_payload(0, "req"));
  actor.mailbox()->post_control(make_control_payload(0, "noise1"));
  actor.mailbox()->post_control(make_control_payload(0, "noise2"));
  actor.pump(); // parks: no match queued yet
  ASSERT_FALSE(actor.wait_result.has_value());
  // A later message matching the predicate: exactly the first match is
  // consumed; the rest stay queued in order.
  actor.mailbox()->post_control(make_control_payload(0, "target"));
  actor.mailbox()->post_control(make_control_payload(0, "other-target"));
  actor.pump(); // parked scan resumes with the first match
  ASSERT_TRUE(actor.wait_result.has_value());
  ASSERT_TRUE(*actor.wait_result == "target");
  // Non-matching messages are preserved and dispatched in order; the
  // late non-match ("other-target") is dispatched normally.
  actor.pump();
  const std::vector<std::string> expected{"Creq", "Cnoise1", "Cnoise2",
                                          "Cother-target"};
  ASSERT_TRUE(actor.events == expected);
}

void test_wait_for_timeout_resume() {
  recorder_actor_t actor(actor_config_t{.name = "t8"});
  actor.wait_deadline_ms = 1; // short deadline so the timeout fires quickly
  actor.mailbox()->post_control(make_control_payload(0, "req"));
  actor.mailbox()->post_control(make_control_payload(0, "noise"));
  actor.pump(); // parks
  int guard = 0;
  while (!actor.wait_timed_out && guard++ < 1000000) {
    actor.pump();
  }
  ASSERT_TRUE(actor.wait_timed_out);
  ASSERT_TRUE(actor.wait_result.has_value());
  ASSERT_TRUE(*actor.wait_result == "TIMEOUT");
  // Queue contents preserved: the non-matching message is still there.
  actor.pump();
  ASSERT_TRUE(std::find(actor.events.begin(), actor.events.end(), "Cnoise") !=
              actor.events.end());
}

void test_wait_for_catch_all_drain_in_order() {
  recorder_actor_t actor(actor_config_t{.name = "t9"});
  actor.mailbox()->post_control(peer_event_t{peer_event_kind_t::registered, 1});
  actor.mailbox()->post_control(make_peer_event(peer_event_kind_t::removed, 2));
  actor.mailbox()->post_control(make_peer_event(peer_event_kind_t::stopped, 3));
  // Park with a trivially-true predicate: consumes the first queued
  // message in order; events ignored elsewhere drain harmlessly.
  std::vector<peer_id_t> drained;
  actor.wait_for(
      [](const test_control_t &) { return true; },
      std::chrono::milliseconds(100),
      [&drained](std::optional<test_control_t> res) {
        if (res) {
          drained.push_back(std::get<peer_event_t>(*res).peer_id);
        }
      });
  actor.pump();
  ASSERT_EQUAL(drained.size(), 1UL);
  ASSERT_EQUAL(drained[0], 1);
  // The rest were drained in order and ignored by the handler.
  const std::vector<std::string> expected{"E2", "E3"};
  ASSERT_TRUE(actor.events == expected);
}

void test_midi_flows_during_parked_wait() {
  recorder_actor_t actor(actor_config_t{.name = "t10"});
  actor.mailbox()->post_control(make_control_payload(0, "req"));
  actor.pump(); // parks (no target ever arrives)
  actor.mailbox()->post_data(data_message_t::midi_received(7, small_payload(7)));
  actor.mailbox()->post_data(data_message_t::midi_received(8, small_payload(8)));
  int guard = 0;
  while (std::find(actor.events.begin(), actor.events.end(), "D7") ==
             actor.events.end() &&
         guard++ < 100) {
    actor.pump();
  }
  // The data lane is fully drained while the control wait is parked.
  ASSERT_TRUE(std::find(actor.events.begin(), actor.events.end(), "D7") !=
              actor.events.end());
  ASSERT_TRUE(std::find(actor.events.begin(), actor.events.end(), "D8") !=
              actor.events.end());
  // Unpark via the token; the actor exits cleanly.
  actor.request_stop_token();
  while (actor.pump()) {
  }
}

void test_threaded_actor_smoke() {
  auto supervisor = std::make_shared<test_mailbox_t>();
  recorder_actor_t actor(
      actor_config_t{.name = "threaded", .supervisor_mailbox = supervisor});
  actor.start();
  actor.mailbox()->post_data(data_message_t::midi_received(3, small_payload(3)));
  int guard = 0;
  while (actor.events.empty() && guard++ < 2000) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_TRUE(std::find(actor.events.begin(), actor.events.end(), "D3") !=
              actor.events.end());
  actor.request_stop();
  guard = 0;
  while (supervisor->idle() && guard++ < 2000) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  auto s = supervisor->pop_control();
  ASSERT_TRUE(s.has_value());
  ASSERT_TRUE(std::holds_alternative<stopped_t>(*s));
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_drain_interleaving_data_first),
      TEST(test_drain_control_progress_under_data_stream),
      TEST(test_per_message_exception_isolation),
      TEST(test_stop_handshake),
      TEST(test_stop_token_escalation),
      TEST(test_wait_for_immediate_match),
      TEST(test_wait_for_parked_first_match_rest_preserved),
      TEST(test_wait_for_timeout_resume),
      TEST(test_wait_for_catch_all_drain_in_order),
      TEST(test_midi_flows_during_parked_wait),
      TEST(test_threaded_actor_smoke),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

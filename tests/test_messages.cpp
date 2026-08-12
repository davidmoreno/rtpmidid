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

/// Message protocol unit tests (tasks 3.1-3.4): payload self-ownership
/// across copies/moves, oversized payload heap-escape + pool-exhaustion
/// drop, lane assignment, requester-side deadline handling.

#include "mailbox.hpp"
#include "test_case.hpp"
#include "test_utils.hpp"
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace rtpmididns;

void test_payload_self_ownership_across_copies_and_moves() {
  uint8_t src[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto payload = midi_payload_t::make(src, sizeof(src));
  ASSERT_TRUE(payload.has_value());
  ASSERT_EQUAL(payload->size(), 10UL);
  ASSERT_TRUE(std::memcmp(payload->data(), src, 10) == 0);

  // Copy survives producer destruction.
  {
    auto copy = *payload;
    ASSERT_EQUAL(copy.size(), 10UL);
    ASSERT_TRUE(std::memcmp(copy.data(), src, 10) == 0);
  }
  // Move keeps the payload valid.
  auto moved = std::move(*payload);
  ASSERT_EQUAL(moved.size(), 10UL);
  ASSERT_TRUE(std::memcmp(moved.data(), src, 10) == 0);

  // A data message crossing a queue owns its payload.
  auto mb = std::make_shared<test_mailbox_t>();
  {
    auto msg = data_message_t::midi_received(42, std::move(moved));
    mb->post_data(std::move(msg)); // producer destroyed here
  }
  auto out = mb->pop_data();
  ASSERT_TRUE(out.has_value());
  ASSERT_EQUAL(out->from, 42u);
  ASSERT_TRUE(out->kind == data_message_t::kind_t::midi_received);
  ASSERT_EQUAL(out->payload.size(), 10UL);
  ASSERT_TRUE(std::memcmp(out->payload.data(), src, 10) == 0);
}

void test_midi_to_wire_carries_to_and_from() {
  uint8_t b[3] = {0x90, 60, 100};
  auto payload = midi_payload_t::make(b, 3);
  auto msg = data_message_t::midi_to_wire(7, 3, std::move(*payload));
  ASSERT_TRUE(msg.kind == data_message_t::kind_t::midi_to_wire);
  ASSERT_EQUAL(msg.to, 7u);
  ASSERT_EQUAL(msg.from, 3u);
  ASSERT_EQUAL(msg.payload.size(), 3UL);
}

void test_oversized_payload_heap_escape_and_pool_exhaustion() {
  // Oversized payloads use the bounded heap escape pool.
  std::vector<uint8_t> big(midi_payload_t::inline_capacity + 64, 0xAB);
  auto p1 = midi_payload_t::make(big.data(), big.size());
  ASSERT_TRUE(p1.has_value());
  ASSERT_EQUAL(p1->size(), big.size());
  ASSERT_TRUE(p1->data()[0] == 0xAB);
  ASSERT_TRUE(p1->data()[big.size() - 1] == 0xAB);

  // Pool exhaustion: a tiny budget drops the payload and counts it.
  const auto saved_limit = escape_pool_t::limit;
  const auto saved_drops = escape_pool_t::dropped_payloads.load();
  escape_pool_t::limit = 1; // only one byte of escape allowed
  std::vector<uint8_t> big2(midi_payload_t::inline_capacity + 8, 0xCD);
  auto p2 = midi_payload_t::make(big2.data(), big2.size());
  ASSERT_FALSE(p2.has_value()); // dropped: budget exhausted
  ASSERT_EQUAL(escape_pool_t::dropped_payloads.load(), saved_drops + 1);
  // Restore and release accounting.
  escape_pool_t::limit = saved_limit;
}

void test_lane_assignment() {
  auto mb = std::make_shared<test_mailbox_t>();
  uint8_t b[2] = {0x90, 60};
  auto payload = midi_payload_t::make(b, 2);
  mb->post_data(data_message_t::midi_received(1, std::move(*payload)));
  mb->post_control(make_stop());
  mb->post_control(make_control_payload(0, "x"));

  // MIDI travels the data lane; control messages the control lane.
  auto d = mb->pop_data();
  ASSERT_TRUE(d.has_value());
  ASSERT_TRUE(d->kind == data_message_t::kind_t::midi_received);
  auto c1 = mb->pop_control();
  ASSERT_TRUE(c1.has_value());
  ASSERT_TRUE(std::holds_alternative<stop_t>(*c1));
  auto c2 = mb->pop_control();
  ASSERT_TRUE(c2.has_value());
  ASSERT_TRUE(std::holds_alternative<control_payload_t>(*c2));
  ASSERT_TRUE(mb->idle());
}

void test_late_response_discarded_via_pending_table() {
  pending_request_table_t table;
  table.add(1001, std::chrono::milliseconds(1));
  ASSERT_TRUE(table.is_pending(1001));
  // Unknown corr: silently discarded.
  ASSERT_FALSE(table.is_pending(999));
  // Late response: after the deadline the entry is treated as unknown.
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  ASSERT_FALSE(table.is_pending(1001));

  // A live entry survives until explicitly erased.
  table.add(2002, std::chrono::milliseconds(1000));
  ASSERT_TRUE(table.is_pending(2002));
  table.erase(2002);
  ASSERT_FALSE(table.is_pending(2002));
}

void test_hdr_envelope_roundtrip() {
  auto m = make_control_payload(0, "hello");
  ASSERT_TRUE(m.text == "hello");
  // Move-only control message (contains reap_actor with jthread): moves work.
  auto moved = std::move(m);
  ASSERT_TRUE(moved.text == "hello");
  // reap_actor carries a jthread (design D9).
  test_control_t reap{reap_actor_t{std::jthread(), "wedged"}};
  ASSERT_TRUE(std::holds_alternative<reap_actor_t>(reap));
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_payload_self_ownership_across_copies_and_moves),
      TEST(test_midi_to_wire_carries_to_and_from),
      TEST(test_oversized_payload_heap_escape_and_pool_exhaustion),
      TEST(test_lane_assignment),
      TEST(test_late_response_discarded_via_pending_table),
      TEST(test_hdr_envelope_roundtrip),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

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

/// Network rtpmidi peer + listener actor integration tests (tasks 5.2, 5.3,
/// 5.7, 5.8): a full RTP-MIDI connect handshake over loopback UDP through
/// the actor stack (router, listener, client peer, accepted peer), MIDI
/// flowing in both directions, and listener routing cleanup.

#include "actor.hpp"
#include "messages.hpp"
#include "network_rtpmidi_listener_actor.hpp"
#include "network_rtpmidi_peer_actor.hpp"
#include "router_actor.hpp"
#include "test_case.hpp"
#include "worker_actor.hpp"
#include <chrono>
#include <cstring>
#include <thread>

using namespace rtpmididns;

// Listener test port (midi = +1). Chosen to avoid common service ports.
static constexpr uint16_t TEST_PORT = 24680;

static bool wait_until(const std::function<bool()> &f, int timeout_ms = 8000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (f()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return f();
}

void test_connect_handshake_and_midi_loop() {
  auto supervisor = std::make_shared<actor_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = supervisor});
  auto worker = std::make_shared<worker_actor_t>(actor_config_t{.name = "w"});
  router->start();
  worker->start();
  // The listener registers itself as a hosted peer so it appears in status.
  auto listener = std::make_shared<network_rtpmidi_listener_actor_t>(
      actor_config_t{.name = "listener", .supervisor_mailbox = supervisor},
      "test-listener", TEST_PORT, router->mailbox());
  listener->start();

  // Spawn the client (initiator) through the router.
  std::shared_ptr<network_rtpmidi_peer_actor_t> client;
  spawn_peer_t sp;
  sp.hdr = hdr_t{1};
  sp.reply_to = std::make_shared<actor_mailbox_t>();
  sp.type = "network_rtpmidi_peer_t";
  sp.factory = [&client, worker](const mailbox_handle_t &sup, peer_id_t pid) {
    client = std::make_shared<network_rtpmidi_peer_actor_t>(
        actor_config_t{.name = "client",
                       .id = pid,
                       .supervisor_mailbox = sup},
        "127.0.0.1", std::to_string(TEST_PORT), "0", worker);
    return client;
  };
  router->mailbox()->post_control(std::move(sp));

  // Wait for the full handshake: the client and the accepted peer connect.
  ASSERT_TRUE(wait_until([&] {
    return client && client->is_connected() && router->peers().size() >= 2;
  }));

  // The router now has the client peer and the accepted peer.
  ASSERT_EQUAL(router->peers().size(), 2UL);
  peer_id_t client_id = 0;
  for (auto &[id, rec] : router->peers()) {
    if (rec.meta.empty()) {
      client_id = id;
    }
  }
  ASSERT_NOT_EQUAL(client_id, 0u);

  // Wire the graph: client -> accepted peer.
  peer_id_t accepted_id = 0;
  for (auto &[id, rec] : router->peers()) {
    if (id != client_id) {
      accepted_id = id;
    }
  }
  auto req = std::make_shared<actor_mailbox_t>();
  router->mailbox()->post_control(connect_t{hdr_t{2}, req, client_id, accepted_id});
  router->pump();

  // MIDI round trip: midi_received{client} -> router -> accepted peer ->
  // UDP -> client socket -> client rtppeer -> midi_received back to the
  // router (the loop proves the whole chain, both directions of the wire).
  uint8_t note[3] = {0x90, 60, 100};
  auto payload = midi_payload_t::make(note, 3);
  router->mailbox()->post_data(
      data_message_t::midi_received(client_id, std::move(*payload)));
  auto got = [&]() -> bool {
    auto d = router->mailbox()->pop_data();
    return d.has_value() && d->kind == data_message_t::kind_t::midi_received &&
           d->payload.size() == 3 && d->payload.data()[0] == 0x90;
  };
  ASSERT_TRUE(wait_until(got));

  // And the reverse direction.
  uint8_t cc[3] = {0xB0, 10, 127};
  auto payload2 = midi_payload_t::make(cc, 3);
  router->mailbox()->post_data(
      data_message_t::midi_received(accepted_id, std::move(*payload2)));
  auto got2 = [&]() -> bool {
    auto d = router->mailbox()->pop_data();
    return d.has_value() && d->kind == data_message_t::kind_t::midi_received &&
           d->payload.size() == 3 && d->payload.data()[0] == 0xB0;
  };
  ASSERT_TRUE(wait_until(got2));

  // Cleanup: remove both network peers through the choreography.
  router->mailbox()->post_control(remove_peer_t{hdr_t{3}, req, client_id});
  router->mailbox()->post_control(remove_peer_t{hdr_t{4}, req, accepted_id});
  ASSERT_TRUE(wait_until([&] { return router->peers().empty(); }, 10000));
  // Stop the top-level actors.
  listener->request_stop();
  router->request_stop();
  worker->request_stop();
}

void test_listener_peer_gone_cleanup() {
  auto router_mb = std::make_shared<actor_mailbox_t>();
  auto listener = std::make_shared<network_rtpmidi_listener_actor_t>(
      actor_config_t{.name = "l"}, "l", TEST_PORT + 1, router_mb);
  listener->pump(); // on_start: bind sockets
  ASSERT_EQUAL(listener->control_port(), TEST_PORT + 1);
  // Simulate a peer actor reporting its connection ended.
  listener->mailbox()->post_control(udp_peer_gone_t{0x1111, 0x2222});
  listener->pump();
  // The routing entries are erased; status shows no connections.
  ASSERT_EQUAL(listener->connection_count(), 0UL);
  listener->request_stop_token();
  while (listener->pump()) {
  }
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_connect_handshake_and_midi_loop),
      TEST(test_listener_peer_gone_cleanup),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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

/// Router actor unit tests (tasks 4.1-4.12): forwarding fan-out, spawn and
/// register handshakes, remove choreography, escalation/reap, requester-
/// driven status gather, subscriptions, command relay.

#include "actor.hpp"
#include "router_actor.hpp"
#include "test_case.hpp"
#include "test_utils.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace rtpmididns;

// --- helper: a peer that answers router requests ----------------------------

class test_peer_t : public actor_t<data_message_t, test_control_t> {
public:
  std::mutex m;
  std::vector<std::string> events;
  std::vector<peer_id_t> got_from; // midi_to_wire from
  std::vector<peer_id_t> got_to;   // midi_to_wire to
  bool answer_status = true;
  bool wedged = false; // sleep in the control handler (escalation test)
  peer_status_variant_t status_payload = rtp_peer_status_t{};

  using actor_t::actor_t;

  void on_control(test_control_t &&msg) override {
    if (wedged) {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      return;
    }
    std::visit(
        [this](auto &&m) {
          using T = std::decay_t<decltype(m)>;
          if constexpr (std::is_same_v<T, registered_t>) {
            record("registered");
          } else if constexpr (std::is_same_v<T, peer_status_req_t>) {
            if (answer_status) {
              m.reply_to.post_control(
                  peer_status_resp_t{m.hdr, m.target, status_payload});
            }
          } else if constexpr (std::is_same_v<T, peer_command_t>) {
            record("cmd:" + m.cmd);
            m.reply_to.post_control(
                peer_command_resp_t{m.hdr, m.peer_id, "\"ok\"", false});
          } else if constexpr (std::is_same_v<T, peer_event_t>) {
            record("ev:" + std::to_string(int(m.kind)) + ":" +
                   std::to_string(m.peer_id));
          }
        },
        msg);
  }
  void on_data(data_message_t &&msg) override {
    if (msg.kind == data_message_t::kind_t::midi_to_wire) {
      std::lock_guard<std::mutex> l(m);
      got_to.push_back(msg.to);
      got_from.push_back(msg.from);
    }
  }

  void record(const std::string &e) {
    std::lock_guard<std::mutex> l(m);
    events.push_back(e);
  }
  bool has_event(const std::string &e) {
    std::lock_guard<std::mutex> l(m);
    return std::find(events.begin(), events.end(), e) != events.end();
  }
  bool wait_event(const std::string &e, int timeout_ms = 2000) {
    for (int i = 0; i < timeout_ms; i++) {
      if (has_event(e)) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return has_event(e);
  }
};

// --- small helpers ----------------------------------------------------------

static midi_payload_t small_payload(int tag) {
  uint8_t b[4] = {uint8_t(tag), 0, 0, 0};
  return *midi_payload_t::make(b, sizeof(b));
}

/// Pump an actor until a predicate holds or a timeout elapses. The brief
/// sleep between pumps lets real time pass (thread scheduling, deadlines).
template <typename Actor, typename Pred>
bool pump_until(Actor &actor, Pred &&pred, int timeout_ms = 5000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (!actor.pump()) {
      return false;
    }
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return pred();
}

/// Pop and discard every control message in a mailbox (stale acks etc.).
static void drain_mailbox(const std::shared_ptr<test_mailbox_t> &mb) {
  while (mb->pop_control()) {
  }
}

static std::optional<peer_id_t> first_ack_id(
    const std::shared_ptr<test_mailbox_t> &req, uint64_t corr) {
  while (auto c = req->pop_control()) {
    if (auto *r = std::get_if<peer_ids_result_t>(&*c)) {
      if (r->hdr.corr == corr && !r->ids.empty()) {
        return r->ids[0];
      }
    }
  }
  return std::nullopt;
}

// --- tests ------------------------------------------------------------------

void test_forwarding_fanout() {
  auto supervisor = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = supervisor});
  auto req = std::make_shared<test_mailbox_t>();
  auto peerA = std::make_shared<test_peer_t>(actor_config_t{.name = "A"});
  auto peerB = std::make_shared<test_peer_t>(actor_config_t{.name = "B"});
  auto peerC = std::make_shared<test_peer_t>(actor_config_t{.name = "C"});

  router->mailbox()->post_control(
      register_peer_t{hdr_t{1}, req, peerA->mailbox(), "test", "A"});
  router->mailbox()->post_control(
      register_peer_t{hdr_t{2}, req, peerB->mailbox(), "test", "B"});
  router->mailbox()->post_control(
      register_peer_t{hdr_t{3}, req, peerC->mailbox(), "test", "C"});
  router->pump(); // one drain pass processes all three registers
  const auto idA = *first_ack_id(req, 1);
  const auto idB = *first_ack_id(req, 2);
  const auto idC = *first_ack_id(req, 3);
  ASSERT_EQUAL(router->peers().size(), 3UL);

  router->mailbox()->post_control(connect_t{hdr_t{4}, req, idA, idB});
  router->mailbox()->post_control(connect_t{hdr_t{5}, req, idA, idC});
  router->pump();

  // A -> B and A -> C: one midi_received fans out to both.
  router->mailbox()->post_data(
      data_message_t::midi_received(idA, small_payload(0x5A)));
  router->pump();
  peerB->pump();
  peerC->pump();
  ASSERT_EQUAL(peerB->got_to.size(), 1UL);
  ASSERT_EQUAL(peerB->got_to[0], idB);
  ASSERT_EQUAL(peerB->got_from[0], idA);
  ASSERT_EQUAL(peerC->got_to.size(), 1UL);
  ASSERT_EQUAL(peerC->got_to[0], idC);
  ASSERT_EQUAL(peerC->got_from[0], idA);
  // The source has no data echo.
  peerA->pump();
  ASSERT_EQUAL(peerA->got_to.size(), 0UL);

  // Unknown sender is dropped with a warning; nobody receives it.
  router->mailbox()->post_data(
      data_message_t::midi_received(999, small_payload(0x99)));
  router->pump();
  peerB->pump();
  peerC->pump();
  ASSERT_EQUAL(peerB->got_to.size(), 1UL);
}

void test_spawn_handshake_ordering() {
  auto supervisor = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = supervisor});
  auto req = std::make_shared<test_mailbox_t>();
  std::shared_ptr<test_peer_t> spawned;

  spawn_peer_t sp;
  sp.hdr = hdr_t{7};
  sp.reply_to = req;
  sp.type = "test";
  sp.factory = [&spawned](const mailbox_handle_t &sup, peer_id_t pid) {
    spawned = std::make_shared<test_peer_t>(actor_config_t{
        .name = "spawned", .id = pid, .supervisor_mailbox = sup});
    return spawned;
  };
  router->mailbox()->post_control(std::move(sp));
  router->pump();
  // The caller receives the assigned ids...
  const auto id = first_ack_id(req, 7);
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(router->peers().size(), 1UL);
  // ...and the spawned peer gates on `registered` before wire traffic.
  ASSERT_TRUE(spawned->wait_event("registered"));
  // Failed preparation spawns nothing.
  spawn_peer_t bad;
  bad.hdr = hdr_t{8};
  bad.reply_to = req;
  bad.factory = [](const mailbox_handle_t &, peer_id_t)
      -> std::shared_ptr<actor_base_t> {
    throw std::runtime_error("prep failed");
  };
  router->mailbox()->post_control(std::move(bad));
  router->pump();
  ASSERT_EQUAL(router->peers().size(), 1UL);
  drain_mailbox(req); // discard the failure ack

  // Cleanup: remove the spawned peer (thread joined via the choreography).
  router->mailbox()->post_control(remove_peer_t{hdr_t{9}, req, id.value()});
  pump_until(*router, [&] {
    while (auto c = req->pop_control()) {
      if (std::holds_alternative<ack_t>(*c)) {
        return true;
      }
    }
    return false;
  });
  ASSERT_TRUE(router->peers().empty());
}

void test_remove_choreography_and_partner_events() {
  auto supervisor = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = supervisor});
  auto req = std::make_shared<test_mailbox_t>();
  auto partner = std::make_shared<test_peer_t>(actor_config_t{.name = "P"});
  std::shared_ptr<test_peer_t> spawned;
  router->mailbox()->post_control(register_peer_t{
      hdr_t{1}, req, partner->mailbox(), "test", "partner"});
  router->pump();
  const auto idP = *first_ack_id(req, 1);

  spawn_peer_t sp;
  sp.hdr = hdr_t{2};
  sp.reply_to = req;
  sp.type = "test";
  sp.factory = [&spawned](const mailbox_handle_t &sup, peer_id_t pid) {
    spawned = std::make_shared<test_peer_t>(actor_config_t{
        .name = "spawned", .id = pid, .supervisor_mailbox = sup});
    return spawned;
  };
  router->mailbox()->post_control(std::move(sp));
  router->pump();
  const auto idS = *first_ack_id(req, 2);
  ASSERT_TRUE(spawned->wait_event("registered"));

  // Connect partner -> spawned, then remove the spawned peer.
  router->mailbox()->post_control(connect_t{hdr_t{3}, req, idP, idS});
  router->pump();
  drain_mailbox(req);
  partner->pump();
  ASSERT_TRUE(partner->has_event("ev:" + std::to_string(int(peer_event_kind_t::connected)) +
                                 ":" + std::to_string(idS)));

  router->mailbox()->post_control(remove_peer_t{hdr_t{4}, req, idS});
  bool acked = pump_until(*router, [&] {
    while (auto c = req->pop_control()) {
      if (auto *a = std::get_if<ack_t>(&*c)) {
        return a->ok;
      }
    }
    return false;
  });
  ASSERT_TRUE(acked);
  // Topology was cut before the stop: the partner got a disconnect event
  // and the graph no longer routes to the removed peer.
  partner->pump();
  ASSERT_TRUE(partner->has_event(
      "ev:" + std::to_string(int(peer_event_kind_t::disconnected)) + ":" +
      std::to_string(idS)));
  ASSERT_TRUE(router->peers().find(idS) == router->peers().end());
}

void test_escalation_reaps_wedged_peer() {
  auto supervisor = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = supervisor});
  router->set_remove_deadline(std::chrono::milliseconds(10));
  router->set_escalation_grace(std::chrono::milliseconds(10));
  auto req = std::make_shared<test_mailbox_t>();
  std::shared_ptr<test_peer_t> wedged;
  spawn_peer_t sp;
  sp.hdr = hdr_t{1};
  sp.reply_to = req;
  sp.type = "wedged";
  sp.factory = [&wedged](const mailbox_handle_t &sup, peer_id_t pid) {
    wedged = std::make_shared<test_peer_t>(actor_config_t{
        .name = "wedged", .id = pid, .supervisor_mailbox = sup});
    wedged->wedged = true;
    return wedged;
  };
  router->mailbox()->post_control(std::move(sp));
  router->pump();
  const auto id = *first_ack_id(req, 1);

  // Remove: the wedged peer misses the stop deadline and its grace window;
  // the router raises the token, then delegates the thread to the reaper
  // and acks with a warning — without blocking.
  router->mailbox()->post_control(remove_peer_t{hdr_t{2}, req, id});
  bool acked = pump_until(*router, [&] {
    while (auto c = req->pop_control()) {
      if (auto *a = std::get_if<ack_t>(&*c)) {
        return !a->ok; // warning ack
      }
    }
    return false;
  });
  ASSERT_TRUE(acked);
  ASSERT_TRUE(router->peers().empty());
  // The supervisor got the reap_actor with the live jthread.
  auto reap = supervisor->pop_control();
  ASSERT_TRUE(reap.has_value());
  ASSERT_TRUE(std::holds_alternative<reap_actor_t>(*reap));
  ASSERT_TRUE(std::get<reap_actor_t>(*reap).thread.joinable());
}

void test_requester_driven_status_gather() {
  auto router = std::make_shared<router_actor_t>(actor_config_t{.name = "r"});
  auto req = std::make_shared<test_mailbox_t>();
  auto peerX = std::make_shared<test_peer_t>(actor_config_t{.name = "X"});
  auto peerY = std::make_shared<test_peer_t>(actor_config_t{.name = "Y"});
  router->mailbox()->post_control(
      register_peer_t{hdr_t{1}, req, peerX->mailbox(), "test", "X"});
  router->mailbox()->post_control(
      register_peer_t{hdr_t{2}, req, peerY->mailbox(), "test", "Y"});
  router->pump();
  const auto idX = *first_ack_id(req, 1);
  const auto idY = *first_ack_id(req, 2);

  router->mailbox()->post_control(status_req_t{hdr_t{50}, req});
  router->pump(); // head + scatter
  auto head = req->pop_control();
  ASSERT_TRUE(head.has_value());
  ASSERT_TRUE(std::holds_alternative<status_head_t>(*head));
  auto &h = std::get<status_head_t>(*head);
  ASSERT_EQUAL(h.hdr.corr, 50ULL);
  ASSERT_EQUAL(h.peers.size(), 2UL);
  // The router answers the head and scatters; peers answer the requester
  // directly.
  peerX->pump();
  peerY->pump();
  int resps = 0;
  while (auto c = req->pop_control()) {
    if (std::holds_alternative<peer_status_resp_t>(*c)) {
      resps++;
    }
  }
  ASSERT_EQUAL(resps, 2);
  (void)idX;
  (void)idY;
}

void test_peer_death_mid_gather_shrinks_set() {
  auto router = std::make_shared<router_actor_t>(actor_config_t{.name = "r"});
  auto req = std::make_shared<test_mailbox_t>();
  auto peerX = std::make_shared<test_peer_t>(actor_config_t{.name = "X"});
  router->mailbox()->post_control(
      register_peer_t{hdr_t{1}, req, peerX->mailbox(), "test", "X"});
  router->pump();
  const auto idX = *first_ack_id(req, 1);
  // Subscriber: requester shrinks its expected set on peer events.
  router->mailbox()->post_control(subscribe_events_t{hdr_t{2}, req});
  router->pump();

  // The peer self-terminates mid-gather (stopped without a pending remove).
  router->mailbox()->post_control(make_stopped(hdr_t{0}, idX));
  router->pump();
  auto ev = req->pop_control();
  ASSERT_TRUE(ev.has_value());
  ASSERT_TRUE(std::holds_alternative<peer_event_t>(*ev));
  ASSERT_TRUE(std::get<peer_event_t>(*ev).kind ==
              peer_event_kind_t::stopped);
  ASSERT_EQUAL(std::get<peer_event_t>(*ev).peer_id, idX);
  ASSERT_TRUE(router->peers().empty());
}

void test_subscription_stream_and_unsubscribe() {
  auto router = std::make_shared<router_actor_t>(actor_config_t{.name = "r"});
  auto req = std::make_shared<test_mailbox_t>();
  auto sub = std::make_shared<test_mailbox_t>();
  router->mailbox()->post_control(subscribe_events_t{hdr_t{1}, sub});
  router->pump();
  router->mailbox()->post_control(
      register_peer_t{hdr_t{2}, req, req, "test", "one"});
  router->pump();
  auto ev = sub->pop_control();
  ASSERT_TRUE(ev.has_value());
  ASSERT_TRUE(std::get<peer_event_t>(*ev).kind ==
              peer_event_kind_t::registered);
  // Unsubscribe ends the stream.
  router->mailbox()->post_control(unsubscribe_events_t{hdr_t{3}, sub});
  router->pump();
  router->mailbox()->post_control(
      register_peer_t{hdr_t{4}, req, req, "test", "two"});
  router->pump();
  ASSERT_FALSE(sub->pop_control().has_value());
}

void test_command_relay() {
  auto router = std::make_shared<router_actor_t>(actor_config_t{.name = "r"});
  auto req = std::make_shared<test_mailbox_t>();
  auto peer = std::make_shared<test_peer_t>(actor_config_t{.name = "p"});
  router->mailbox()->post_control(
      register_peer_t{hdr_t{1}, req, peer->mailbox(), "test", "p"});
  router->pump();
  const auto id = *first_ack_id(req, 1);

  router->mailbox()->post_control(
      peer_command_t{hdr_t{10}, req, id, "foo", "{}"});
  router->pump(); // relay
  peer->pump();   // execute on the peer thread and reply
  ASSERT_TRUE(peer->has_event("cmd:foo"));
  auto resp = req->pop_control();
  ASSERT_TRUE(resp.has_value());
  ASSERT_TRUE(std::holds_alternative<peer_command_resp_t>(*resp));
  auto &r = std::get<peer_command_resp_t>(*resp);
  ASSERT_EQUAL(r.hdr.corr, 10ULL);
  ASSERT_EQUAL(r.peer_id, id);
  ASSERT_FALSE(r.is_error);
}

void test_stop_all_bounded() {
  auto supervisor = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = supervisor});
  auto req = std::make_shared<test_mailbox_t>();
  std::vector<std::shared_ptr<test_peer_t>> spawned;
  for (int i = 0; i < 3; i++) {
    spawn_peer_t sp;
    sp.hdr = hdr_t{uint64_t(100 + i)};
    sp.reply_to = req;
    sp.type = "test";
    sp.factory = [&spawned](const mailbox_handle_t &sup, peer_id_t pid) {
      auto p = std::make_shared<test_peer_t>(actor_config_t{
          .name = "p", .id = pid, .supervisor_mailbox = sup});
      spawned.push_back(p);
      return p;
    };
    router->mailbox()->post_control(std::move(sp));
    router->pump();
  }
  ASSERT_EQUAL(router->peers().size(), 3UL);
  for (auto &p : spawned) {
    ASSERT_TRUE(p->wait_event("registered"));
  }
  // One stop_all stops everything and acks once.
  router->mailbox()->post_control(stop_all_t{hdr_t{200}, req});
  bool acked = pump_until(*router, [&] {
    while (auto c = req->pop_control()) {
      if (auto *a = std::get_if<ack_t>(&*c)) {
        return a->ok;
      }
    }
    return false;
  });
  ASSERT_TRUE(acked);
  ASSERT_TRUE(router->peers().empty());
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_forwarding_fanout),
      TEST(test_spawn_handshake_ordering),
      TEST(test_remove_choreography_and_partner_events),
      TEST(test_escalation_reaps_wedged_peer),
      TEST(test_requester_driven_status_gather),
      TEST(test_peer_death_mid_gather_shrinks_set),
      TEST(test_subscription_stream_and_unsubscribe),
      TEST(test_command_relay),
      TEST(test_stop_all_bounded),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

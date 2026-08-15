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

/// Peer actor base, rawmidi peer actor and worker actor unit tests
/// (tasks 5.5, 5.6, 5.7, 6.1, 6.2): registered gate, self-termination on
/// gate timeout, status/command message handling, rawmidi recv->message
/// and message->send, worker FIFO jobs and DNS resolution.

#include "local_rawmidi_peer_actor.hpp"
#include "mdns_actor.hpp"
#include "peer_actor.hpp"
#include "test_case.hpp"
#include "test_utils.hpp"
#include "worker_actor.hpp"
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace rtpmididns;

// --- helper -----------------------------------------------------------------

class fake_peer_t : public peer_actor_t {
public:
  std::vector<peer_id_t> sent_to;
  std::vector<peer_id_t> sent_from;
  std::vector<std::vector<uint8_t>> sent_payload;
  peer_status_variant_t status_payload = rawmidi_peer_status_t{};

  using peer_actor_t::peer_actor_t;

  void send_to_wire(peer_id_t to, peer_id_t from,
                    midi_payload_t &&payload) override {
    sent_to.push_back(to);
    sent_from.push_back(from);
    sent_payload.emplace_back(payload.data(), payload.data() + payload.size());
  }
  peer_status_variant_t status() override { return status_payload; }
  std::string get_type() const override { return "fake_peer_t"; }
};

static midi_payload_t small_payload(int tag) {
  uint8_t b[4] = {uint8_t(tag), 0, 0, 0};
  return *midi_payload_t::make(b, sizeof(b));
}

// --- peer base: registered gate (5.1) ---------------------------------------

void test_registered_gate_drops_pre_registration_traffic() {
  auto router = std::make_shared<test_mailbox_t>();
  fake_peer_t peer(actor_config_t{.name = "p", .id = 3,
                                  .supervisor_mailbox = router});
  // Wire traffic before `registered`: not processed (dropped).
  peer.mailbox()->post_data(
      data_message_t::midi_to_wire(1, 2, small_payload(0x10)));
  peer.pump();
  ASSERT_TRUE(peer.sent_to.empty());
  // The gate consumes `registered` and wire traffic flows.
  peer.mailbox()->post_control(registered_t{{5}});
  peer.pump();
  ASSERT_TRUE(peer.registered());
  ASSERT_EQUAL(peer.registered_ids().size(), 1UL);
  peer.mailbox()->post_data(
      data_message_t::midi_to_wire(9, 8, small_payload(0x20)));
  peer.pump();
  ASSERT_EQUAL(peer.sent_to.size(), 1UL);
  ASSERT_EQUAL(peer.sent_to[0], 9u);
  ASSERT_EQUAL(peer.sent_from[0], 8u);
  ASSERT_EQUAL(peer.sent_payload[0][0], 0x20);
}

void test_registered_gate_timeout_self_terminates() {
  auto router = std::make_shared<test_mailbox_t>();
  fake_peer_t peer(actor_config_t{.name = "p", .id = 7,
                                  .supervisor_mailbox = router});
  peer.set_registered_timeout(std::chrono::milliseconds(1));
  int guard = 0;
  while (peer.pump() && guard++ < 10000) {
  }
  // Self-terminated: `stopped` was posted to the router with the peer id.
  auto s = router->pop_control();
  ASSERT_TRUE(s.has_value());
  ASSERT_TRUE(std::holds_alternative<stopped_t>(*s));
  ASSERT_EQUAL(std::get<stopped_t>(*s).peer_id, 7u);
}

// --- peer base: status/command as message handlers (5.6) ---------------------

void test_peer_status_and_command_messages() {
  auto router = std::make_shared<test_mailbox_t>();
  fake_peer_t peer(actor_config_t{.name = "p", .id = 3,
                                  .supervisor_mailbox = router});
  auto req = std::make_shared<test_mailbox_t>();
  peer.mailbox()->post_control(registered_t{{3}});
  peer.pump();

  rawmidi_peer_status_t st;
  st.name = "midi0";
  peer.status_payload = st;
  peer.mailbox()->post_control(peer_status_req_t{hdr_t{11}, req, 3});
  peer.pump();
  auto resp = req->pop_control();
  ASSERT_TRUE(resp.has_value());
  ASSERT_TRUE(std::holds_alternative<peer_status_resp_t>(*resp));
  auto &ps = std::get<peer_status_resp_t>(*resp);
  ASSERT_EQUAL(ps.hdr.corr, 11ULL);
  ASSERT_EQUAL(ps.peer_id, 3u);
  ASSERT_TRUE(std::holds_alternative<rawmidi_peer_status_t>(ps.status));

  // Commands execute on the peer thread and reply to the requester.
  peer.mailbox()->post_control(
      peer_command_t{hdr_t{12}, req, 3, "status", "{}"});
  peer.pump();
  auto cr = req->pop_control();
  ASSERT_TRUE(cr.has_value());
  ASSERT_TRUE(std::holds_alternative<peer_command_resp_t>(*cr));
  auto &pc = std::get<peer_command_resp_t>(*cr);
  ASSERT_EQUAL(pc.hdr.corr, 12ULL);
  ASSERT_FALSE(pc.is_error);
  // Unknown command: error result.
  peer.mailbox()->post_control(
      peer_command_t{hdr_t{13}, req, 3, "bogus", "{}"});
  peer.pump();
  auto ce = req->pop_control();
  ASSERT_TRUE(ce.has_value());
  ASSERT_TRUE(std::get<peer_command_resp_t>(*ce).is_error);
}

// --- rawmidi peer actor (5.5) ------------------------------------------------

void test_rawmidi_actor_recv_to_message_and_message_to_send() {
  int sv[2] = {-1, -1};
  ASSERT_EQUAL(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  auto router = std::make_shared<test_mailbox_t>();
  local_rawmidi_peer_actor_t peer(
      actor_config_t{.name = "raw", .id = 4, .supervisor_mailbox = router},
      "testdev", "raw", sv[0]);

  peer.pump(); // on_start: registers the fd in the actor poller + the gate

  // Recv path: MIDI bytes on the fd become midi_received to the router.
  uint8_t note_on[3] = {0x90, 60, 100};
  ASSERT_EQUAL(write(sv[1], note_on, 3), 3);
  peer.run_once(std::chrono::milliseconds(1)); // service the fd
  peer.pump(); // drain the posted message? (message goes to the router)
  auto d = router->pop_data();
  ASSERT_TRUE(d.has_value());
  ASSERT_TRUE(d->kind == data_message_t::kind_t::midi_received);
  ASSERT_EQUAL(d->from, 4u);
  ASSERT_EQUAL(d->payload.size(), 3UL);
  ASSERT_TRUE(std::memcmp(d->payload.data(), note_on, 3) == 0);

  // Send path: midi_to_wire (after registered) writes to the fd.
  peer.mailbox()->post_control(registered_t{{4}});
  peer.pump();
  peer.mailbox()->post_data(
      data_message_t::midi_to_wire(5, 4, small_payload(0x33)));
  peer.pump();
  uint8_t out[4] = {};
  ssize_t n = read(sv[1], out, sizeof(out));
  ASSERT_EQUAL(n, (ssize_t)4);
  ASSERT_EQUAL(out[0], 0x33);
  ::close(sv[1]);
}

// --- worker actor (6.1, 6.2) -------------------------------------------------

void test_worker_jobs_fifo_and_exception_isolation() {
  worker_actor_t worker(actor_config_t{.name = "w"});
  std::vector<int> ran;
  worker.enqueue([&ran] { ran.push_back(1); });
  worker.enqueue([&ran] {
    throw std::runtime_error("job boom");
  });
  worker.enqueue([&ran] { ran.push_back(2); });
  worker.pump();
  // FIFO order; the throwing job was isolated.
  const std::vector<int> expected{1, 2};
  ASSERT_TRUE(ran == expected);
}

void test_worker_dns_resolution() {
  worker_actor_t worker(actor_config_t{.name = "w"});
  auto req = std::make_shared<test_mailbox_t>();
  resolve_dns(worker, "localhost", "5004", req, 42);
  worker.pump();
  auto r = req->pop_control();
  ASSERT_TRUE(r.has_value());
  ASSERT_TRUE(std::holds_alternative<dns_resolved_t>(*r));
  auto &dns = std::get<dns_resolved_t>(*r);
  ASSERT_EQUAL(dns.hdr.corr, 42ULL);
  ASSERT_TRUE(dns.hostname == "localhost");
  ASSERT_FALSE(dns.addresses.empty()); // 127.0.0.1 and/or ::1
}


// --- mdns discovery -> waiting ALSA port only (lazy model) --------------------

void test_mdns_discovery_creates_waiting_port_only() {
  auto alsa_mb = std::make_shared<alsa_mailbox_t>();
  auto server_mb = std::make_shared<server_mailbox_t>();
  auto mdns = std::make_shared<mdns_actor_t>(
      actor_config_t{.name = "mdns"}, alsa_mb, server_mb);
  mdns->pump(); // on_start (no avahi: mdns_ null, but wiring works)

  // Discovery event (lazy model, task 2.1): the mdns actor asks the ALSA
  // listener for a waiting port and records the target on the rtpmidi
  // server. No network client is spawned.
  mdns->on_discovered("Fancy Synth", "192.168.1.50", "5004");
  mdns->pump();
  auto req = alsa_mb->pop_control();
  ASSERT_TRUE(req.has_value());
  ASSERT_TRUE(std::holds_alternative<alsa_create_port_t>(*req));
  auto &cp = std::get<alsa_create_port_t>(*req);
  ASSERT_TRUE(cp.name == "Fancy Synth");
  ASSERT_TRUE(cp.waiting);
  ASSERT_TRUE(cp.remote == "Fancy Synth");
  // The server learns the outbound target.
  auto srv = server_mb->pop_control();
  ASSERT_TRUE(srv.has_value());
  ASSERT_TRUE(std::holds_alternative<server_remote_discovered_t>(*srv));
  auto &sd = std::get<server_remote_discovered_t>(*srv);
  ASSERT_TRUE(sd.remote == "Fancy Synth" && sd.address == "192.168.1.50" &&
              sd.port == "5004");

  // The ALSA listener replies with the seq port of the waiting port.
  mdns->mailbox()->post_control(alsa_port_result_t{cp.hdr, 7, 0});
  mdns->pump();

  // Removal: the waiting port and the target are removed; no router traffic.
  mdns->on_removed("Fancy Synth");
  mdns->pump();
  bool saw_alsa_remove = false;
  while (auto c = alsa_mb->pop_control()) {
    if (auto *rm = std::get_if<alsa_remove_port_t>(&*c)) {
      saw_alsa_remove = (rm->seq_port == 7);
    }
  }
  ASSERT_TRUE(saw_alsa_remove);
  bool saw_gone = false;
  while (auto c = server_mb->pop_control()) {
    if (auto *g = std::get_if<server_remote_gone_t>(&*c)) {
      saw_gone = (g->remote == "Fancy Synth");
    }
  }
  ASSERT_TRUE(saw_gone);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_registered_gate_drops_pre_registration_traffic),
      TEST(test_registered_gate_timeout_self_terminates),
      TEST(test_peer_status_and_command_messages),
      TEST(test_rawmidi_actor_recv_to_message_and_message_to_send),
      TEST(test_worker_jobs_fifo_and_exception_isolation),
      TEST(test_worker_dns_resolution),
      TEST(test_mdns_discovery_creates_waiting_port_only),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

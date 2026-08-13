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

/// ALSA <-> network bridging tests: the common "external device into
/// aseqdump" flow (tasks 5.4).
///
/// 1. The "Network Export" bridge: an ALSA client connecting to an
///    announced port makes rtpmidid initiate the rtpmidi session to the
///    discovered remotes (alsa_port_event_t -> connect in the router).
/// 2. The MIDI redirection itself, with a real ALSA sequencer: notes
///    written into the daemon's ALSA port reach the network peer, and
///    network MIDI reaches an ALSA subscriber (aseqdump).
///
/// The ALSA tests need a working kernel sequencer (/dev/snd/seq); when
/// unavailable they pass vacuously (CI containers usually lack it).

#include "alsa_actor.hpp"
#include "mdns_actor.hpp"
#include "router_actor.hpp"
#include "test_case.hpp"
#include "test_utils.hpp"
#include <alsa/asoundlib.h>
#include <chrono>
#include <thread>

using namespace rtpmididns;

// --- helpers ----------------------------------------------------------------

static bool wait_until(const std::function<bool()> &f, int timeout_ms = 5000) {
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

/// A raw ALSA client used as the "external device" / "aseqdump" stand-in.
struct seq_test_client_t {
  snd_seq_t *seq = nullptr;
  int port = -1;

  bool open(const char *name) {
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) <
        0) {
      return false;
    }
    snd_seq_set_client_name(seq, name);
    port = snd_seq_create_simple_port(
        seq, "test", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE |
                         SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);
    return port >= 0;
  }
  ~seq_test_client_t() {
    if (seq) {
      snd_seq_close(seq);
    }
  }

  /// Subscribe this port to the daemon port (receive its output).
  bool receive_from(int daemon_client, int daemon_port) {
    return snd_seq_connect_from(seq, port, daemon_client, daemon_port) >= 0;
  }
  /// Send a NoteOn directly into the daemon port.
  void send_noteon(int daemon_client, int daemon_port, uint8_t note = 60,
                   uint8_t vel = 100) {
    snd_seq_event_t ev {};
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_noteon(&ev, 0, note, vel);
    snd_seq_ev_set_source(&ev, port);
    snd_seq_ev_set_dest(&ev, daemon_client, daemon_port);
    snd_seq_ev_set_direct(&ev);
    snd_seq_event_output(seq, &ev);
    snd_seq_drain_output(seq);
  }
  /// Read a pending NoteOn (if any) and return its note value.
  std::optional<uint8_t> read_noteon() {
    snd_seq_event_t *ev = nullptr;
    while (snd_seq_event_input_pending(seq, 0) > 0) {
      if (snd_seq_event_input(seq, &ev) < 0) {
        continue;
      }
      if (ev->type == SND_SEQ_EVENT_NOTEON) {
        return ev->data.note.note;
      }
    }
    return std::nullopt;
  }
};

// --- 1. the announce-port subscribe bridge -----------------------------------

/// The mdns actor, on an ALSA subscription to an announced port, wires the
/// port to every discovered remote (initiates the rtpmidi sessions).
void test_mdns_subscribe_bridge_wires_discovered_remotes() {
  auto router_mb = std::make_shared<router_mailbox_t>();
  auto alsa_mb = std::make_shared<alsa_mailbox_t>();
  auto worker = std::make_shared<worker_actor_t>(actor_config_t{.name = "w"});
  auto mdns = std::make_shared<mdns_actor_t>(
      actor_config_t{.name = "mdns"}, router_mb, alsa_mb, worker);
  mdns->pump();

  // A discovered remote with a spawned, connected client (net id 20).
  mdns->on_discovered("RemoteSynth", "192.168.1.50", "5004");
  mdns->pump();
  auto req = alsa_mb->pop_control();
  ASSERT_TRUE(req.has_value());
  auto &cp = std::get<alsa_create_port_t>(*req);
  mdns->mailbox()->post_control(peer_ids_result_t{cp.hdr, {10}, {}});
  mdns->pump();
  auto spawn = router_mb->pop_control();
  auto &sp = std::get<spawn_peer_t>(*spawn);
  mdns->mailbox()->post_control(peer_ids_result_t{sp.hdr, {20}, {}});
  mdns->pump();

  // Drain the discovery wiring's own connect messages.
  while (router_mb->pop_control()) {
  }

  // An ALSA client subscribes to the announced "Network Export" port: the
  // router connects that port (id 99) to the discovered remote's client.
  mdns->mailbox()->post_control(alsa_port_event_t{99, true});
  mdns->pump();
  int connects = 0;
  while (auto c = router_mb->pop_control()) {
    if (auto *ct = std::get_if<connect_t>(&*c)) {
      connects++;
      ASSERT_TRUE((ct->from == 99 && ct->to == 20) ||
                  (ct->from == 20 && ct->to == 99));
    }
  }
  ASSERT_EQUAL(connects, 2);

  // Unsubscribe: the graph edges are cut.
  mdns->mailbox()->post_control(alsa_port_event_t{99, false});
  mdns->pump();
  int disconnects = 0;
  while (auto c = router_mb->pop_control()) {
    if (std::get_if<disconnect_t>(&*c)) {
      disconnects++;
    }
  }
  ASSERT_EQUAL(disconnects, 2);
}

/// The ALSA actor posts alsa_port_event on subscribe/unsubscribe to an
/// announced port (real sequencer; vacuously passes when unavailable).
void test_alsa_announce_subscribe_initiates_session() {
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox =
                                           std::make_shared<test_mailbox_t>()});
  router->start();
  auto mdns_mb = std::make_shared<mdns_mailbox_t>();
  auto alsa = std::make_shared<alsa_actor_t>(
      actor_config_t{.name = "alsa", .supervisor_mailbox =
                                          std::make_shared<test_mailbox_t>()},
      "rtpmidid-test", std::vector<std::string>{"Network Export"},
      router->mailbox(), mdns_mb);
  alsa->start();
  // Real sequencer unavailable (e.g. CI container): pass vacuously.
  if (!alsa->seq()) {
    router->request_stop();
    alsa->request_stop();
    return;
  }
  // The announced port is registered with the router.
  ASSERT_TRUE(wait_until([&] {
    for (auto &[id, rec] : router->peers()) {
      if (rec.type == "alsa") {
        return true;
      }
    }
    return false;
  }));
  peer_id_t alsa_port_id = 0;
  for (auto &[id, rec] : router->peers()) {
    if (rec.type == "alsa") {
      alsa_port_id = id;
    }
  }
  ASSERT_NOT_EQUAL(alsa_port_id, 0u);
  const auto ports = alsa->announced_ports();
  ASSERT_EQUAL(ports.size(), 1UL);

  // An external ALSA client connects to the announced port: the actor must
  // notify the mdns actor to initiate the rtpmidi session.
  seq_test_client_t client;
  ASSERT_TRUE(client.open("rtpmidid-test-device"));
  ASSERT_TRUE(client.receive_from(alsa->seq()->client_id, ports[0]));
  ASSERT_TRUE(wait_until([&] {
    while (auto c = mdns_mb->pop_control()) {
      if (auto *e = std::get_if<alsa_port_event_t>(&*c)) {
        return e->subscribed && e->port_id == alsa_port_id;
      }
    }
    return false;
  }));
  // Disconnect: the session is torn down.
  snd_seq_disconnect_from(client.seq, client.port, alsa->seq()->client_id,
                          ports[0]);
  ASSERT_TRUE(wait_until([&] {
    while (auto c = mdns_mb->pop_control()) {
      if (auto *e = std::get_if<alsa_port_event_t>(&*c)) {
        return !e->subscribed && e->port_id == alsa_port_id;
      }
    }
    return false;
  }));
  router->request_stop();
  alsa->request_stop();
}

// --- 2. MIDI redirection: external device <-> aseqdump -----------------------

void test_alsa_midi_bridging() {
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox =
                                           std::make_shared<test_mailbox_t>()});
  router->start();
  auto alsa = std::make_shared<alsa_actor_t>(
      actor_config_t{.name = "alsa", .supervisor_mailbox =
                                          std::make_shared<test_mailbox_t>()},
      "rtpmidid-test", std::vector<std::string>{"Network Export"},
      router->mailbox());
  alsa->start();
  if (!alsa->seq()) {
    router->request_stop();
    alsa->request_stop();
    return; // no real sequencer: pass vacuously
  }

  // The announced ALSA port is registered; find its router id.
  ASSERT_TRUE(wait_until([&] {
    for (auto &[id, rec] : router->peers()) {
      if (rec.type == "alsa") {
        return true;
      }
    }
    return false;
  }));
  peer_id_t alsa_port_id = 0;
  for (auto &[id, rec] : router->peers()) {
    if (rec.type == "alsa") {
      alsa_port_id = id;
    }
  }
  const auto ports = alsa->announced_ports();
  ASSERT_EQUAL(ports.size(), 1UL);

  // A fake network peer (the remote rtpmidi device): registered and wired
  // to the ALSA port through the router.
  auto net_peer = std::make_shared<peer_mailbox_t>();
  auto req = std::make_shared<test_mailbox_t>();
  router->mailbox()->post_control(
      register_peer_t{hdr_t{1}, req, net_peer, "network", "remote"});
  ASSERT_TRUE(wait_until([&] { return !req->idle(); }));
  peer_id_t net_id = 0;
  while (auto c = req->pop_control()) {
    if (auto *r = std::get_if<peer_ids_result_t>(&*c)) {
      net_id = r->ids[0];
    }
  }
  router->mailbox()->post_control(
      connect_t{hdr_t{2}, req, alsa_port_id, net_id});
  router->mailbox()->post_control(
      connect_t{hdr_t{3}, req, net_id, alsa_port_id});
  router->pump();

  // aseqdump: an ALSA client subscribed to the daemon's announced port.
  seq_test_client_t dump;
  ASSERT_TRUE(dump.open("rtpmidid-test-aseqdump"));
  ASSERT_TRUE(dump.receive_from(alsa->seq()->client_id, ports[0]));

  // Direction 1 (external device -> aseqdump): a note written into the
  // daemon's ALSA port reaches the network peer as midi_to_wire.
  dump.send_noteon(alsa->seq()->client_id, ports[0], 60, 100);
  ASSERT_TRUE(wait_until([&] {
    auto d = net_peer->pop_data();
    return d.has_value() &&
           d->kind == data_message_t::kind_t::midi_to_wire &&
           d->payload.size() >= 3 && d->payload.data()[0] == 0x90;
  }));
  // Direction 2 (network -> aseqdump): MIDI from the remote peer is written
  // to the ALSA port and read by the subscribed client.
  uint8_t note[3] = {0x90, 72, 110};
  auto payload = midi_payload_t::make(note, 3);
  router->mailbox()->post_data(
      data_message_t::midi_received(net_id, std::move(*payload)));
  ASSERT_TRUE(wait_until([&] {
    auto n = dump.read_noteon();
    return n.has_value() && *n == 72;
  }));

  router->request_stop();
  alsa->request_stop();
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_mdns_subscribe_bridge_wires_discovered_remotes),
      TEST(test_alsa_announce_subscribe_initiates_session),
      TEST(test_alsa_midi_bridging),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

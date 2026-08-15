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

/// ALSA listener tests (lazy-rtpmidi-connections, tasks 1.5, 2.6):
///
/// 1. Waiting-port semantics: ports for outbound remotes are created
///    unregistered; the first ALSA subscription registers them as hosted
///    peers and posts a session_request to the rtpmidi server; the last
///    unsubscribe unregisters them again.
/// 2. The MIDI redirection itself, with a real ALSA sequencer: notes
///    written into the daemon's ALSA port reach the network peer, and
///    network MIDI reaches an ALSA subscriber (aseqdump).
///
/// The ALSA tests need a working kernel sequencer (/dev/snd/seq); when
/// unavailable they pass vacuously (CI containers usually lack it).

#include "alsa_actor.hpp"
#include "router_actor.hpp"
#include "settings.hpp"
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

/// Stop an actor and wait for its exit notice: the actor thread must be
/// fully done before the object is destroyed (the base-class join runs
/// after the derived members are gone).
static void stop_and_wait(const std::shared_ptr<actor_base_t> &actor,
                          const std::shared_ptr<test_mailbox_t> &supervisor) {
  actor->request_stop();
  wait_until([&] {
    while (auto c = supervisor->pop_control()) {
      if (std::get_if<stopped_t>(&*c)) {
        return true;
      }
    }
    return false;
  }, 10000);
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

// --- 1. waiting-port semantics ------------------------------------------------

/// Waiting ports are created unregistered; the first ALSA subscription
/// registers them and requests the session; the last unsubscribe
/// unregisters them (task 1.2/1.3/1.5).
void test_waiting_port_register_on_subscribe() {
  auto router_sup = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = router_sup});
  router->start();
  auto server_mb = std::make_shared<server_mailbox_t>();
  auto alsa_mb = std::make_shared<alsa_mailbox_t>();
  auto alsa_sup = std::make_shared<test_mailbox_t>();
  auto alsa = std::make_shared<alsa_actor_t>(
      actor_config_t{.name = "alsa", .supervisor_mailbox = alsa_sup},
      "rtpmidid-test", std::vector<std::string>{}, router->mailbox(),
      server_mb);
  alsa->set_mailbox(alsa_mb);
  alsa->start();
  // Real sequencer unavailable (e.g. CI container): pass vacuously.
  if (!alsa->seq()) {
    stop_and_wait(alsa, alsa_sup);
    stop_and_wait(router, router_sup);
    return;
  }

  // Create a waiting port for a remote (as discovery/connect_to do).
  alsa_mb->post_control(alsa_create_port_t{hdr_t{1}, mailbox_handle_t{},
                                           "RemoteSynth", "192.168.1.50:5004",
                                           true, "RemoteSynth"});
  ASSERT_TRUE(wait_until([&] {
    uint8_t sp = 0;
    return alsa->waiting_port_for("RemoteSynth", sp);
  }));
  uint8_t seq_port = 0;
  ASSERT_TRUE(alsa->waiting_port_for("RemoteSynth", seq_port));

  // Idle waiting port: not registered with the router.
  ASSERT_FALSE(alsa->port_registered(seq_port));
  ASSERT_TRUE(router->peers().empty());

  // First ALSA subscription: the port is registered as a hosted peer and
  // a session request lands on the rtpmidi server.
  seq_test_client_t client;
  ASSERT_TRUE(client.open("rtpmidid-test-device"));
  ASSERT_TRUE(client.receive_from(alsa->seq()->client_id, seq_port));
  ASSERT_TRUE(wait_until([&] { return alsa->port_registered(seq_port); }));
  peer_id_t port_id = 0;
  ASSERT_TRUE(wait_until([&] {
    while (auto c = server_mb->pop_control()) {
      if (auto *sr = std::get_if<session_request_t>(&*c)) {
        if (sr->subscribed && sr->remote == "RemoteSynth") {
          port_id = sr->port_id;
          return true;
        }
      }
    }
    return false;
  }));
  ASSERT_NOT_EQUAL(port_id, 0u);
  ASSERT_EQUAL(alsa->port_subscribers(seq_port), 1);

  // Last unsubscribe: the port is unregistered and the session request
  // reports the teardown.
  snd_seq_disconnect_from(client.seq, client.port, alsa->seq()->client_id,
                          seq_port);
  ASSERT_TRUE(wait_until([&] { return !alsa->port_registered(seq_port); }));
  ASSERT_TRUE(wait_until([&] {
    while (auto c = server_mb->pop_control()) {
      if (auto *sr = std::get_if<session_request_t>(&*c)) {
        if (!sr->subscribed && sr->remote == "RemoteSynth") {
          return true;
        }
      }
    }
    return false;
  }));
  ASSERT_TRUE(router->peers().empty());

  stop_and_wait(alsa, alsa_sup);
  stop_and_wait(router, router_sup);
}

/// A waiting port wired to a live inbound session stays registered when
/// the last subscriber leaves (alsa-listener spec): the server marks the
/// session state through `alsa_port_session_t`.
void test_waiting_port_session_keeps_registration() {
  auto router_sup = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = router_sup});
  router->start();
  auto server_mb = std::make_shared<server_mailbox_t>();
  auto alsa_mb = std::make_shared<alsa_mailbox_t>();
  auto alsa_sup = std::make_shared<test_mailbox_t>();
  auto alsa = std::make_shared<alsa_actor_t>(
      actor_config_t{.name = "alsa", .supervisor_mailbox = alsa_sup},
      "rtpmidid-test", std::vector<std::string>{}, router->mailbox(),
      server_mb);
  alsa->set_mailbox(alsa_mb);
  alsa->start();
  if (!alsa->seq()) {
    stop_and_wait(alsa, alsa_sup);
    stop_and_wait(router, router_sup);
    return;
  }

  alsa_mb->post_control(alsa_create_port_t{hdr_t{1}, mailbox_handle_t{},
                                           "RemoteSynth", "", true,
                                           "RemoteSynth"});
  ASSERT_TRUE(wait_until([&] {
    uint8_t sp = 0;
    return alsa->waiting_port_for("RemoteSynth", sp);
  }));
  uint8_t seq_port = 0;
  ASSERT_TRUE(alsa->waiting_port_for("RemoteSynth", seq_port));

  // Subscribe and wait for the registration.
  seq_test_client_t client;
  ASSERT_TRUE(client.open("rtpmidid-test-device"));
  ASSERT_TRUE(client.receive_from(alsa->seq()->client_id, seq_port));
  ASSERT_TRUE(wait_until([&] { return alsa->port_registered(seq_port); }));
  // The server reports a live session on the port (inbound reuse).
  alsa_mb->post_control(alsa_port_session_t{seq_port, true});

  // Last unsubscribe with a live session: the port STAYS registered.
  snd_seq_disconnect_from(client.seq, client.port, alsa->seq()->client_id,
                          seq_port);
  ASSERT_TRUE(wait_until([&] {
    return alsa->port_subscribers(seq_port) == 0;
  }));
  ASSERT_TRUE(alsa->port_registered(seq_port));

  // Session end: with no subscribers the port goes back to waiting.
  alsa_mb->post_control(alsa_port_session_t{seq_port, false});
  ASSERT_TRUE(wait_until([&] { return !alsa->port_registered(seq_port); }));

  stop_and_wait(alsa, alsa_sup);
  stop_and_wait(router, router_sup);
}

// --- 2. MIDI redirection: external device <-> aseqdump -----------------------

void test_alsa_midi_bridging() {
  auto router_sup = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = router_sup});
  router->start();
  auto alsa_sup = std::make_shared<test_mailbox_t>();
  auto alsa = std::make_shared<alsa_actor_t>(
      actor_config_t{.name = "alsa", .supervisor_mailbox = alsa_sup},
      "rtpmidid-test", std::vector<std::string>{"Network Export"},
      router->mailbox());
  alsa->start();
  if (!alsa->seq()) {
    stop_and_wait(alsa, alsa_sup);
    stop_and_wait(router, router_sup);
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

  stop_and_wait(alsa, alsa_sup);
  stop_and_wait(router, router_sup);
}

// --- 3. ALSA hardware auto-export (task 6.4) ---------------------------------

/// Enumerate + hotplug: matching local ports are exported through the
/// rtpmidi server, the daemon's own ports are never exported, and port
/// removal drops the export.
void test_auto_export_tracks_ports() {
  auto saved_export = settings.alsa_hw_auto_export;
  settings.alsa_hw_auto_export.type =
      settings_t::alsa_hw_auto_export_type_e::ALL;
  settings.alsa_hw_auto_export.name_positive_regex =
      std::regex("rtpmidid-autoexp.*");
  settings.alsa_hw_auto_export.name_negative_regex = std::nullopt;

  auto router_sup = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = router_sup});
  router->start();
  auto alsa_sup = std::make_shared<test_mailbox_t>();
  auto server_mb = std::make_shared<server_mailbox_t>();
  auto alsa_mb = std::make_shared<alsa_mailbox_t>();
  auto alsa = std::make_shared<alsa_actor_t>(
      actor_config_t{.name = "alsa", .supervisor_mailbox = alsa_sup},
      // The daemon client name matches the positive regex on purpose: its
      // own ports must still never be exported (no self-loop).
      "rtpmidid-autoexp-daemon", std::vector<std::string>{},
      router->mailbox(), server_mb);
  alsa->set_mailbox(alsa_mb);
  alsa->start();
  if (!alsa->seq()) {
    settings.alsa_hw_auto_export = saved_export;
    stop_and_wait(alsa, alsa_sup);
    stop_and_wait(router, router_sup);
    return; // no real sequencer: pass vacuously
  }

  // Give the startup enumeration a moment; then assert nothing of our own
  // was exported.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto own_prefix = std::to_string(alsa->seq()->client_id) + ":";
  while (auto c = server_mb->pop_control()) {
    if (auto *ea = std::get_if<export_add_t>(&*c)) {
      ASSERT_TRUE(ea->target.find(own_prefix) != 0);
    }
  }

  // Hotplug add: a matching external client appears; the export follows.
  seq_test_client_t ext;
  ASSERT_TRUE(ext.open("rtpmidid-autoexp-src"));
  std::optional<export_add_t> added;
  ASSERT_TRUE(wait_until([&] {
    while (auto c = server_mb->pop_control()) {
      if (auto *ea = std::get_if<export_add_t>(&*c)) {
        if (ea->kind == export_kind_e::seq &&
            ea->target.find(own_prefix) != 0) {
          added = std::move(*ea);
          return true;
        }
        ASSERT_TRUE(ea->target.find(own_prefix) != 0);
      }
    }
    return false;
  }));
  ASSERT_TRUE(added->name.find("rtpmidid-autoexp-src") != std::string::npos);

  // Hotplug remove: the external client disappears; the export goes away.
  snd_seq_close(ext.seq);
  ext.seq = nullptr;
  bool removed = false;
  ASSERT_TRUE(wait_until([&] {
    while (auto c = server_mb->pop_control()) {
      if (auto *er = std::get_if<export_remove_t>(&*c)) {
        removed = er->name == added->name;
        if (removed) {
          return true;
        }
      }
    }
    return false;
  }));
  ASSERT_TRUE(removed);

  settings.alsa_hw_auto_export = saved_export;
  stop_and_wait(alsa, alsa_sup);
  stop_and_wait(router, router_sup);
}

/// A seq-export connection creates the per-connection subscription peer:
/// a daemon port subscribed both ways to the exported local port,
/// registered as a hosted peer and routing MIDI in both directions.
void test_seq_export_connection_creates_subscription_peer() {
  auto router_sup = std::make_shared<test_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = router_sup});
  router->start();
  auto alsa_sup = std::make_shared<test_mailbox_t>();
  auto server_mb = std::make_shared<server_mailbox_t>();
  auto alsa_mb = std::make_shared<alsa_mailbox_t>();
  auto alsa = std::make_shared<alsa_actor_t>(
      actor_config_t{.name = "alsa", .supervisor_mailbox = alsa_sup},
      "rtpmidid-subpeer-test", std::vector<std::string>{},
      router->mailbox(), server_mb);
  alsa->set_mailbox(alsa_mb);
  alsa->start();
  if (!alsa->seq()) {
    stop_and_wait(alsa, alsa_sup);
    stop_and_wait(router, router_sup);
    return; // no real sequencer: pass vacuously
  }

  // The exported local port (an external client's port).
  seq_test_client_t ext;
  ASSERT_TRUE(ext.open("rtpmidid-subpeer-src"));

  // Connect: the listener creates the subscription peer.
  auto req = std::make_shared<test_mailbox_t>();
  alsa_mb->post_control(alsa_subscribe_port_t{
      hdr_t{1}, mailbox_handle_t(req), "rtpmidid-subpeer-src",
      FMT::format("{}:{}", snd_seq_client_id(ext.seq), ext.port)});
  uint8_t sub_port = 0;
  peer_id_t sub_id = 0;
  ASSERT_TRUE(wait_until([&] {
    while (auto c = req->pop_control()) {
      if (auto *r = std::get_if<alsa_port_result_t>(&*c)) {
        sub_port = r->seq_port;
        sub_id = r->peer_id;
        return r->peer_id != 0;
      }
    }
    return false;
  }));
  ASSERT_NOT_EQUAL(sub_port, 0);
  ASSERT_NOT_EQUAL(sub_id, 0u);

  // Wire a fake network peer to the subscription peer through the router.
  auto net_peer = std::make_shared<peer_mailbox_t>();
  auto req2 = std::make_shared<test_mailbox_t>();
  router->mailbox()->post_control(
      register_peer_t{hdr_t{2}, req2, net_peer, "network", "remote"});
  ASSERT_TRUE(wait_until([&] { return !req2->idle(); }));
  peer_id_t net_id = 0;
  while (auto c = req2->pop_control()) {
    if (auto *r = std::get_if<peer_ids_result_t>(&*c)) {
      net_id = r->ids[0];
    }
  }
  router->mailbox()->post_control(
      connect_t{hdr_t{3}, req2, sub_id, net_id});
  router->mailbox()->post_control(
      connect_t{hdr_t{4}, req2, net_id, sub_id});

  // Direction 1 (local port -> network): a note sent to the local port's
  // subscribers reaches the fake network peer.
  {
    snd_seq_event_t ev {};
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_noteon(&ev, 0, 64, 100);
    snd_seq_ev_set_source(&ev, ext.port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_event_output(ext.seq, &ev);
    snd_seq_drain_output(ext.seq);
  }
  ASSERT_TRUE(wait_until([&] {
    auto d = net_peer->pop_data();
    return d.has_value() &&
           d->kind == data_message_t::kind_t::midi_to_wire &&
           d->payload.size() >= 3 && d->payload.data()[0] == 0x90;
  }));

  // Direction 2 (network -> local port): MIDI from the fake peer reaches
  // the external client.
  uint8_t note[3] = {0x90, 72, 110};
  auto payload = midi_payload_t::make(note, 3);
  router->mailbox()->post_data(
      data_message_t::midi_received(net_id, std::move(*payload)));
  ASSERT_TRUE(wait_until([&] {
    auto n = ext.read_noteon();
    return n.has_value() && *n == 72;
  }));

  // Disconnect: the session end removes the subscription peer.
  alsa_mb->post_control(
      alsa_remove_port_t{hdr_t{5}, mailbox_handle_t(req), sub_id, sub_port});
  ASSERT_TRUE(wait_until([&] {
    for (auto &[id, rec] : router->peers()) {
      if (id == sub_id) {
        return false;
      }
    }
    return true;
  }));

  stop_and_wait(alsa, alsa_sup);
  stop_and_wait(router, router_sup);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_waiting_port_register_on_subscribe),
      TEST(test_waiting_port_session_keeps_registration),
      TEST(test_alsa_midi_bridging),
      TEST(test_auto_export_tracks_ports),
      TEST(test_seq_export_connection_creates_subscription_peer),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

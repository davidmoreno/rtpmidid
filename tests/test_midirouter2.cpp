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

#include "../src/aseq.hpp"
#include "../src/factory.hpp"
#include "../src/json.hpp"
#include "../src/mididata.hpp"
#include "../src/midirouter.hpp"
#include "../src/network_rtpmidi_listener.hpp"
#include "../src/settings.hpp"
#include "../tests/test_case.hpp"
#include "local_alsa_listener.hpp"
#include "rtpmidid/rtppeer.hpp"
#include "rtpmidid/udppeer.hpp"
#include "test_utils.hpp"
#include <memory>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <rtpmidid/rtpclient.hpp>

using namespace std::chrono_literals;

namespace rtpmididns {
// NOLINTNEXTLINE
settings_t settings;
// NOLINTNEXTLINE
std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
const char *VERSION = "TEST"; // NOLINT
} // namespace rtpmididns

void test_send_receive_messages() {
  // Setup a similar environment as when running rtpmidid
  auto router = std::make_shared<rtpmididns::midirouter_t>();
  uint8_t test_client_id = 0;
  {
    rtpmididns::settings.alsa_name = "rtpmidid-test";
    // rtpmididns::settings.rtpmidid_name = "rtpmidid-test";
    // rtpmididns::settings.rtpmidid_port = "60004";

    std::shared_ptr<rtpmididns::aseq_t> aseq;
    try {
      aseq =
          std::make_shared<rtpmididns::aseq_t>(rtpmididns::settings.alsa_name);
    } catch (rtpmididns::alsa_connect_exception &exc) {
      ERROR("ALSA CONNECT EXCEPTION: {}", exc.what());
      INFO("Skipping test as ALSA is not available.");
      return;
    }

    test_client_id = aseq->client_id;

    router->add_peer(rtpmididns::make_local_alsa_multi_listener(
        rtpmididns::settings.alsa_name, aseq));

    router->add_peer(rtpmididns::make_network_rtpmidi_multi_listener(
        "rtpmidid-test", "60004", aseq));
  }

  // Now I prepare another local port that will be use dfor communication,
  // and I connect from a remote port.
  //
  //  LOCAL       RTPMIDID   REMOTE
  // ------------+----------+----------------------
  //  ALSA A <-> | -------- | <-> RTPPEER A <-+
  //             |          |                 |
  //  ALSA B <-> | -------- | <-> RTPPEER B <-+
  //
  // With this routes we can just send from and to ALSA A and B, and all data
  // should arreive properly.
  //
  // The order ensures try as many options as possible:
  // 1. ALSA A connects to WR RTPMIDID, RTPPEER A should be announced
  // 2. Connect to RTPPEER A
  // 3. RTPPEER B connects to RTPMIDID, ALSA RTPPEER B should be announced
  // 4. ALSA A connects again this RO to RTPMIDID, nothing should happend
  // 5. We conenct RTPPEER A and B manually
  // 6. We connect to ALSA B RW
  //
  // 7. All data we send from A hsould be read a B, and vice versa.

  std::shared_ptr<rtpmididns::aseq_t> aseq;
  try {
    aseq = std::make_shared<rtpmididns::aseq_t>("TESTING DEVICE");
  } catch (rtpmididns::alsa_connect_exception &exc) {
    ERROR("ALSA CONNECT EXCEPTION: {}", exc.what());
    INFO("Skipping test as ALSA is not available.");
    return;
  }

  // 1. ALSA A connect to WR RTPMIDID
  INFO("1. ALSA A connect to WR RTPMIDID");

  auto alsa_a = aseq->create_port("ALSA A");
  int midi_packets_alsa_a = 0;
  auto midievent_listener_a = aseq->midi_event[alsa_a].connect(
      [&midi_packets_alsa_a](snd_seq_event_t *ev) {
        midi_packets_alsa_a++;
        DEBUG("GOT MIDI DATA A: {}", midi_packets_alsa_a);
      });

  auto alsa_a_to_network_connection = aseq->connect(
      rtpmididns::aseq_t::port_t{aseq->client_id, alsa_a},
      rtpmididns::aseq_t::port_t{test_client_id, 0}); // Connect to network
  poller_wait_until([&router]() { return router->peers.size() == 3; });

  rtpmididns::json_t status = router->status();
  DEBUG("{}", status.dump(2));

  ASSERT_EQUAL(router->peers.size(), 3);

  int port = 0;

  router->for_each_peer<rtpmididns::network_rtpmidi_listener_t>(
      [&port](auto *peer) { port = peer->server.port(); });

  DEBUG("At port {}", port);

  // 2. connect RTPPEER A and test
  INFO("2. connect RTPPEER A and test");

  auto rtppeer_client_a = rtpmidid::rtpclient_t("RTPPEER A");
  rtppeer_client_a.add_server_address("localhost", std::to_string(port));
  poller_wait_until([&rtppeer_client_a]() {
    return rtppeer_client_a.peer.status ==
           rtpmidid::rtppeer_t::status_e::CONNECTED;
  });

  status = router->status();
  DEBUG("{}", status.dump(2));
  ASSERT_EQUAL(router->peers.size(), 3);

  auto data = hex_to_bin("90 40 40");
  rtppeer_client_a.peer.send_midi(data);
  ASSERT_EQUAL(midi_packets_alsa_a, 0);
  poller_wait_until(
      [&midi_packets_alsa_a]() { return midi_packets_alsa_a == 1; });
  // The signal for midi data has been called.
  ASSERT_EQUAL(midi_packets_alsa_a, 1);

  // 3. Connect to RTP MIDI announced Network
  INFO("3. Connect to RTP MIDI announced Network");

  uint8_t device_id = aseq->find_device(rtpmididns::settings.alsa_name);

  auto rtppeer_client_b = rtpmidid::rtpclient_t("RTPPEER B");
  rtppeer_client_b.add_server_address("localhost", "60004");
  poller_wait_until([&rtppeer_client_b]() {
    return rtppeer_client_b.peer.status ==
           rtpmidid::rtppeer_t::status_e::CONNECTED;
  });
  ASSERT_EQUAL(rtppeer_client_b.peer.status,
               rtpmidid::rtppeer_t::status_e::CONNECTED);

  uint8_t port_id_b = aseq->find_port(device_id, "RTPPEER B");
  DEBUG("Got ALSA seq for RTPPEER B at {}:{}", (int)device_id, (int)port_id_b);

  status = router->status();
  DEBUG("{}", status.dump(2));
  //// 2 more peers: the rtpmidi_worker and the alsa_worker.
  ASSERT_EQUAL(router->peers.size(), 5);

  // 4. Connect from network to ALSA A. Nothing of importance should happen
  INFO("4. Connect from network to ALSA A. Nothing of importance should "
       "happen");
  DEBUG("Peer count: {}", router->peers.size());

  // Connect network to alsa a
  auto network_to_alsa_a = aseq->connect( //
      rtpmididns::aseq_t::port_t{test_client_id, 0},
      rtpmididns::aseq_t::port_t{aseq->client_id, alsa_a} //
  );

  poller_wait_for(100ms);
  status = router->status();
  DEBUG("{}", status.dump(2));
  //// No more peers
  DEBUG("Found {} peers", router->peers.size());
  ASSERT_EQUAL(router->peers.size(), 5);

  // 5. Connect rtpmidi A and B manually, just copy data
  INFO("5. Connect rtpmidi A and B manually, just copy data");
  auto rtppeer_client_a_midi_connection =
      rtppeer_client_a.peer.midi_event.connect(
          [&](const rtpmidid::io_bytes_reader &data) {
            DEBUG("Got data grom A to B");
            rtppeer_client_b.peer.send_midi(data);
          });
  auto rtppeer_client_b_midi_connection =
      rtppeer_client_b.peer.midi_event.connect(
          [&](const rtpmidid::io_bytes_reader &data) {
            DEBUG("Got data grom B to A");
            rtppeer_client_a.peer.send_midi(data);
          });

  // 6. Connect to/from ALSA B
  INFO("6. Connect to/from ALSA B");

  ASSERT_NOT_EQUAL(aseq->client_id, device_id);
  auto alsa_b_rw = aseq->create_port("ALSA B R/W");
  auto alsa_a_to_b_connection =
      aseq->connect({aseq->client_id, alsa_b_rw}, {device_id, port_id_b});
  auto alsa_b_to_a_connection =
      aseq->connect({device_id, port_id_b}, {aseq->client_id, alsa_b_rw});

  ASSERT_EQUAL(router->peers.size(), 5);
  int midi_packets_alsa_b = 0;
  auto midievent_listener_b = aseq->midi_event[alsa_b_rw].connect(
      [&midi_packets_alsa_b](snd_seq_event_t *ev) {
        midi_packets_alsa_b++;
        DEBUG("GOT MIDI DATA B: {}", midi_packets_alsa_b);
      });

  // 7. Send from A, received at B
  INFO("7. Send from A, received at B");

  rtpmididns::mididata_to_alsaevents_t mdtoa;
  auto mididata = hex_to_bin("90 64 64");
  auto mididata2 = rtpmididns::mididata_t(mididata);
  ASSERT_EQUAL(midi_packets_alsa_b, 0);
  INFO("Send B -> A");
  mdtoa.mididata_to_evs_f(mididata2, [&](auto *ev) {
    snd_seq_ev_set_source(ev, alsa_a);
    snd_seq_ev_set_subs(ev); // to all subscribers
    snd_seq_ev_set_direct(ev);
    snd_seq_event_output_direct(aseq->seq, ev);
  });
  poller_wait_until([&]() { return midi_packets_alsa_b == 1; });
  ASSERT_EQUAL(midi_packets_alsa_b, 1);

  INFO("Send A -> B");
  // Send B to A
  mididata2 = rtpmididns::mididata_t(mididata);
  mdtoa.mididata_to_evs_f(mididata2, [&](auto *ev) {
    snd_seq_ev_set_source(ev, alsa_b_rw);
    snd_seq_ev_set_subs(ev); // to all subscribers
    snd_seq_ev_set_direct(ev);
    snd_seq_event_output_direct(aseq->seq, ev);
  });
  poller_wait_until([&]() { return midi_packets_alsa_a == 2; });
  ASSERT_EQUAL(midi_packets_alsa_a, 2);

  DEBUG("Router: {}", router->status().dump(2));
  router->clear();
  DEBUG("Router: {}", router->status().dump(2));

  DEBUG("END");
}

void disconnect_all_from_ports(
    std::shared_ptr<rtpmididns::aseq_t> aseq,
    const std::initializer_list<rtpmididns::aseq_t::port_t> &ports) {
  bool alldone = false;
  int count = 0;
  while (!alldone) {
    alldone = true;
    poller_wait_for(100ms); // to force it to do something
    // Disconnect all, as there can be some autoconector as aseqrc

    for (auto source_port : ports) {
      aseq->for_connections(
          source_port, [&](const rtpmididns::aseq_t::port_t &port) {
            DEBUG("Disconnecting: {} -> {}", source_port, port);
            aseq->disconnect(source_port, port);
            aseq->disconnect(port, source_port);
            alldone = false;
          });
    }
    count++;
    ASSERT_LT(count, 10);
  }
}

/**
 ** Test the lifecycle of the ALSA router
 *
 * Will connect ALSA to a detected remote rtpmidi port, should try to connect,
 * then we disconnect it, should be unexported.
 *
 * Then will connect again twice and first should not disconnect, and
 * second should disconnect.
 */
void test_midirouter_alsa_listener_lifecycle() {
  auto router = std::make_shared<rtpmididns::midirouter_t>();

  std::shared_ptr<rtpmididns::aseq_t> aseq;
  std::shared_ptr<rtpmididns::aseq_t> aseq_external;
  try {
    aseq = std::make_shared<rtpmididns::aseq_t>("Internal");
    aseq_external = std::make_shared<rtpmididns::aseq_t>("External");
  } catch (rtpmididns::alsa_connect_exception &exc) {
    ERROR("ALSA CONNECT EXCEPTION: {}", exc.what());
    INFO("Skipping test as ALSA is not available.");
    return;
  }
  ASSERT_EQUAL(router->peers.size(), 0);

  rtpmidid::udppeer_t test_control("localhost", 0);
  rtpmidid::udppeer_t test_midi("localhost",
                                test_control.get_address().port() + 1);

  auto alsanetwork = std::make_shared<rtpmididns::local_alsa_listener_t>(
      "test", "localhost", std::to_string(test_control.get_address().port()),
      aseq);
  router->add_peer(alsanetwork);
  auto internal_port = alsanetwork->alsaport;
  rtpmididns::aseq_t::port_t internal_device_port = {aseq->client_id,
                                                     internal_port};

  ASSERT_EQUAL(router->peers.size(), 1);

  auto external_port1 = aseq_external->create_port("ext1");
  auto external_port2 = aseq_external->create_port("ext2");
  rtpmididns::aseq_t::port_t external_device_port_1 = {aseq_external->client_id,
                                                       external_port1};
  rtpmididns::aseq_t::port_t external_device_port_2 = {aseq_external->client_id,
                                                       external_port2};

  disconnect_all_from_ports(aseq_external, {
                                               external_device_port_1,
                                               external_device_port_2,
                                           });
  disconnect_all_from_ports(aseq, {internal_device_port});
  DEBUG("Devices and ports");
  aseq->for_devices([&](uint8_t device_id, const std::string &device_name,
                        const rtpmididns::aseq_t::client_type_e &client_type) {
    DEBUG("Device: {}. {}", device_id, device_name);
    aseq->for_ports(device_id,
                    [&](uint8_t port_id, const std::string &port_name) {
                      DEBUG("    Port {}. {}", port_id, port_name);
                    });
  });

  INFO("Connect 1");
  // The original bug to find and fix was two connections from the same port,
  // for example first in, then out.
  aseq_external->connect(internal_device_port, external_device_port_1);
  poller_wait_until([&]() { return router->peers.size() == 2; });
  ASSERT_EQUAL(router->peers.size(), 2);
  INFO("Ok, connected, now disconnect 1");

  aseq_external->disconnect(internal_device_port, external_device_port_1);
  poller_wait_until([&]() { return router->peers.size() == 1; });
  ASSERT_EQUAL(router->peers.size(), 1);
  INFO("Ok, disconnected, connect 2 ports");

  aseq_external->connect(internal_device_port, external_device_port_1);
  poller_wait_until([&]() { return router->peers.size() == 2; });
  ASSERT_EQUAL(router->peers.size(), 2);
  INFO("Ok, connected 2 (1/2) to one listener, reuse the rtpclient, disconnect "
       "1");

  aseq_external->connect(internal_device_port, external_device_port_2);
  poller_wait_for(10ms); // to force it to do something
  poller_wait_until([&]() { return router->peers.size() == 2; });
  ASSERT_EQUAL(router->peers.size(), 2);

  INFO("Ok, connected 2 (2/2) to one listener, reuse the rtpclient, disconnect "
       "1");

  aseq_external->disconnect(internal_device_port, external_device_port_1);
  poller_wait_for(10ms); // to force it to do something
  poller_wait_until([&]() { return router->peers.size() == 2; });
  ASSERT_EQUAL(router->peers.size(), 2);
  INFO("Ok, disconnected 1, still peer exists");

  aseq_external->disconnect(internal_device_port, external_device_port_2);
  poller_wait_until([&]() { return router->peers.size() == 1; });
  ASSERT_EQUAL(router->peers.size(), 1);
  INFO("Ok, disconnected 2, peer not exists");
}

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_send_receive_messages),
      TEST(test_midirouter_alsa_listener_lifecycle),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

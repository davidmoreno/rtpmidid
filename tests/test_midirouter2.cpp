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

#include "../src2/aseq.hpp"
#include "../src2/factory.hpp"
#include "../src2/json.hpp"
#include "../src2/midirouter.hpp"
#include "../src2/rtpmidiserverworker.hpp"
#include "../src2/settings.hpp"
#include "../tests/test_case.hpp"
#include "rtpmidid/rtpserver.hpp"
#include "test_utils.hpp"
#include <chrono>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <rtpmidid/rtpclient.hpp>

using namespace std::chrono_literals;

namespace rtpmididns {
settings_t settings;
std::unique_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
const char *VERSION = "TEST";
} // namespace rtpmididns

void test_send_receive_messages() {
  // Setup a similar environment as when running rtpmidid
  auto router = std::make_shared<rtpmididns::midirouter_t>();
  uint8_t test_client_id;
  {
    rtpmididns::settings.alsa_name = "rtpmidid-test";
    rtpmididns::settings.rtpmidid_name = "rtpmidid-test";
    rtpmididns::settings.rtpmidid_port = "60004";

    auto aseq =
        std::make_shared<rtpmididns::aseq_t>(rtpmididns::settings.alsa_name);
    test_client_id = aseq->client_id;

    router->add_peer(
        rtpmididns::make_alsalistener(rtpmididns::settings.alsa_name, aseq));

    router->add_peer(rtpmididns::make_rtpmidilistener(
        rtpmididns::settings.rtpmidid_name, rtpmididns::settings.rtpmidid_port,
        aseq));
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
  // 2. RTPPEER B connects to RTPMIDID, ALSA B should be announced
  // 3. ALSA A connects again this RO to RTPMIDID, nothing should happend
  // 4. We conenct RTPPEER A and B manually
  // 5. We connect to ALSA B RW
  //
  // 6. All data we send from A hsould be read a B, and vice versa.

  auto aseq =
      std::make_shared<rtpmididns::aseq_t>(rtpmididns::settings.alsa_name);

  // 1. ALSA A connect to WR RTPMIDID

  auto alsa_a = aseq->create_port("ALSA A");
  int midi_packets_alsa_a = 0;
  auto midievent_listener = aseq->midi_event[alsa_a].connect(
      [&midi_packets_alsa_a](snd_seq_event_t *ev) {
        DEBUG("GOT MIDI DATA!");
        midi_packets_alsa_a++;
      });

  DEBUG("CONNECT");
  aseq->connect(
      rtpmididns::aseq_t::port_t{aseq->client_id, alsa_a},
      rtpmididns::aseq_t::port_t{test_client_id, 0}); // Connect to network
  DEBUG("CONNECTED");
  DEBUG("POLL 100ms");
  poller_wait_for(100ms);
  DEBUG("ASEQ AT {}", (void *)aseq.get());

  rtpmididns::json_t status = router->status();
  DEBUG("{}", status.dump(2));

  ASSERT_EQUAL(router->peers.size(), 3);

  int port = 0;

  router->for_each_peer<rtpmididns::rtpmidiserverworker_t>(
      [&port](auto *peer) { port = peer->server.control_port; });

  DEBUG("At port {}", port);

  auto client = rtpmidid::rtpclient_t("RTPPEER A");
  client.connect_to("localhost", std::to_string(port));
  poller_wait_for(200ms);

  status = router->status();
  DEBUG("{}", status.dump(2));
  ASSERT_EQUAL(router->peers.size(), 3);

  auto data = hex_to_bin("90 40 40");
  client.peer.send_midi(data);
  poller_wait_for(100ms);
  ASSERT_EQUAL(midi_packets_alsa_a, 1);

  DEBUG("END");
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_send_receive_messages),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

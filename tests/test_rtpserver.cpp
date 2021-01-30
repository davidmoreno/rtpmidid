/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019 David Moreno Montero <dmoreno@coralbits.com>
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

#include "../tests/test_case.hpp"
#include "../tests/test_utils.hpp"
#include "rtpmidid/iobytes.hpp"
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpserver.hpp>

auto connect_msg = hex_to_bin("FF FF 'IN'"
                              "0000 0002"    // Protocol
                              "00 12 34 00"  // Initiator
                              "00 BE EF 00"  // SSRC
                              "'first' 00"); // Name
auto disconnect_msg = hex_to_bin("FF FF 'BY'"
                                 "0000 0002"
                                 "00 12 34 00"
                                 "00 BE EF 00"
                                 "'first' 00");

// Note on 60, vel 7f
auto midi_msg = hex_to_bin("80 61"
                           "0000"          // seq nr
                           "0000 0000"     // timestamp
                           "00 BE EF 00"   // dest SSRC -- will be changed
                           "03 90 60 7f"); // Lenght + data

// Another peer
auto connect_msg2 = hex_to_bin("FF FF 'IN'"
                               "0000 0002"
                               "00 56 78 00"
                               "00 DE EB 00"
                               "'second' 00");
auto disconnect_msg2 = hex_to_bin("FF FF 'BY'"
                                  "0000 0002"
                                  "00 56 78 00"
                                  "00 DE EB 00");

void test_several_connect_to_server() {
  // random port
  rtpmidid::rtpserver server("test", "0");

  DEBUG("Server port is {}", server.control_port);

  test_client_t control_client(0, server.control_port);
  test_client_t midi_client(control_client.local_port + 1, server.midi_port);

  control_client.send(connect_msg);
  midi_client.send(connect_msg);

  // wait 1 event loop cycle (all pending events)
  ASSERT_EQUAL(server.initiator_to_peer.size(), 1);

  // And reverse order, first arrives the midi event
  test_client_t control_client2(0, server.control_port);
  test_client_t midi_client2(control_client2.local_port + 1, server.midi_port);

  midi_client2.send(connect_msg2);
  control_client2.send(connect_msg2);

  ASSERT_EQUAL(server.initiator_to_peer.size(), 2);
  for (auto &x : server.initiator_to_peer) {
    DEBUG("PEER {:#04x} - {}", x.first, (void *)&x.second);
    DEBUG("InitID {:#04x}, SSRC local {:#04x} remote {:#04x} ",
          x.second->initiator_id, x.second->local_ssrc, x.second->remote_ssrc);
  }
  ASSERT_EQUAL(server.ssrc_to_peer.size(), 2);

  for (auto &peer : server.initiator_to_peer) {
    ASSERT_TRUE(peer.second->is_connected());
  }

  control_client.send(disconnect_msg2);

  // Removed ok
  ASSERT_EQUAL(server.initiator_to_peer.size(), 1);
  ASSERT_EQUAL(server.ssrc_to_peer.size(), 1);

  // Should do nothing, no add, no remove
  midi_client.send(disconnect_msg2); // This may provoke a warning, but can be
                                     // sent by remote side

  DEBUG("{} {} {}", (void *)&server, server.initiator_to_peer.size(),
        server.ssrc_to_peer.size());
  ASSERT_EQUAL(server.initiator_to_peer.size(), 1);
  ASSERT_EQUAL(server.ssrc_to_peer.size(), 1);

  // Disconnect reverse order, same result
  midi_client.send(disconnect_msg);
  control_client.send(disconnect_msg); // This may provoke a warning, but can
                                       // be sent by remote side

  for (auto &x : server.initiator_to_peer) {
    DEBUG("TIP FOR ERROR: STILL HERE? PEER {:#04x} - {}", x.first,
          (void *)&x.second);
    DEBUG("TIP FOR ERROR: STILL HERE? InitID {:#04x}, SSRC local {:#04x} "
          "remote {:#04x} ",
          x.second->initiator_id, x.second->local_ssrc, x.second->remote_ssrc);
  }

  ASSERT_EQUAL(server.initiator_to_peer.size(), 0);
  ASSERT_EQUAL(server.ssrc_to_peer.size(), 0);
}

void test_connect_disconnect_send() {
  rtpmidid::rtpserver server("test", "0");

  test_client_t control_client(0, server.control_port);
  test_client_t midi_client(control_client.local_port + 1, server.midi_port);

  auto nmidievents = std::make_shared<int>(0);
  server.midi_event.connect([&nmidievents](const rtpmidid::io_bytes_reader &) {
    *nmidievents += 1;
    DEBUG("Got MIDI Event");
  });

  control_client.send(connect_msg);
  midi_client.send(connect_msg);

  // Fix midi_msg destination
  // auto peer = server.initiator_to_peer.begin()->second;
  // DEBUG("Server's SSRC {:04X}", peer->local_ssrc);
  // auto writer = rtpmidid::io_bytes_writer(midi_msg);
  // writer.seek(8);
  // writer.write_uint32(peer->local_ssrc);

  // This I receive
  midi_client.send(midi_msg);
  DEBUG("Got {} events", *nmidievents);
  ASSERT_EQUAL(*nmidievents, 1);

  control_client.send(disconnect_msg);
  midi_client.send(disconnect_msg);

  // This should not
  midi_client.send(midi_msg);

  ASSERT_EQUAL(*nmidievents, 1);
}

int main(void) {
  test_case_t testcase{
      TEST(test_several_connect_to_server),
      TEST(test_connect_disconnect_send),
  };

  testcase.run();

  return testcase.exit_code();
}

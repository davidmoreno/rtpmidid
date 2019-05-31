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

#include "../src/rtpserver.hpp"
#include "../src/poller.hpp"
#include "../tests/test_case.hpp"
#include "../tests/test_utils.hpp"

auto connect_msg = hex_to_bin(
  "FF FF 'IN'"
  "0000 0002"
  "'fast' 'beef'"
  "'testing' 00"
);

auto connect_msg2 = hex_to_bin(
  "FF FF 'IN'"
  "0000 0002"
  "'slow' 'deer'"
  "'testing' 00"
);

void test_several_connect_to_server(){
  // random port
  rtpmidid::rtpserver server("test", 0);

  DEBUG("Server port is {}", server.control_port);

  test_client_t control_client(0, server.control_port);
  test_client_t midi_client(control_client.local_port + 1, server.midi_port);

  control_client.send(*connect_msg);
  midi_client.send(*connect_msg);

  // Nothing yet. Need to do some poller cycles.
  ASSERT_EQUAL(server.initiator_to_peer.size(), 0);

  rtpmidid::poller.wait(); // wait 1 event loop cycle (all pending events)
  ASSERT_EQUAL(server.initiator_to_peer.size(), 1);

  // And reverse order, first arrives the midi event
  test_client_t control_client2(0, server.control_port);
  test_client_t midi_client2(control_client2.local_port + 1, server.midi_port);

  midi_client2.send(*connect_msg2);
  rtpmidid::poller.wait();
  control_client2.send(*connect_msg2);
  rtpmidid::poller.wait();

  ASSERT_EQUAL(server.initiator_to_peer.size(), 2);



  rtpmidid::poller.close();
}


int main(void){
  test_case_t testcase{
    TEST(test_several_connect_to_server),
  };

  testcase.run();

  return testcase.exit_code();
}

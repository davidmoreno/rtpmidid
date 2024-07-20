/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/packet.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpclient.hpp>
#include <rtpmidid/rtpmidipacket.hpp>
#include <rtpmidid/rtpserver.hpp>
#include <rtpmidid/udppeer.hpp>
#include <unistd.h>

static std::string get_hostname();

void test_network_address_list() {
  rtpmidid::network_address_list_t localhost_resolutions("localhost", "13001");

  int count = 0;
  for (auto &addr : localhost_resolutions) {
    DEBUG("Address: {}", addr.to_string());
    DEBUG("Hostname: {}", addr.hostname());
    count += 1;
  }
  ASSERT_GT(count, 0);

  // Another way, using iterator directly
  rtpmidid::network_address_list_t google_resolutions("google.com", "https");

  count = 0;
  auto I = google_resolutions.begin();
  auto endI = google_resolutions.end();
  for (; I != endI; ++I) {
    auto &addr = *I;
    DEBUG("Address: {}", addr.to_string());
    DEBUG("Hostname: {}", addr.hostname());
    count += 1;
  }
  ASSERT_GT(count, 0);

  // Just get first
  auto first = localhost_resolutions.get_first();
  ASSERT_EQUAL(first.hostname(), "localhost");
  ASSERT_EQUAL(first.ip(), "127.0.0.1");

  // Move first
  localhost_resolutions =
      std::move(rtpmidid::network_address_list_t("::", "13001"));
  first = localhost_resolutions.get_first();
  DEBUG("First: {}", first.to_string());
  ASSERT_EQUAL(first.hostname(), "::");
  ASSERT_EQUAL(first.ip(), "::");
}

void test_udppeer() {
  rtpmidid::rtpclient_t client("Test");

  DEBUG("Open peerA");
  rtpmidid::udppeer_t peerA("localhost", "13001");
  DEBUG("Open peerB");
  rtpmidid::udppeer_t peerB(
      rtpmidid::network_address_list_t("127.0.0.2", "13002"));

  DEBUG("Get addresses");
  auto peerA_address = peerA.get_address();
  auto peerB_address = peerB.get_address();
  DEBUG("PeerA address: {}", peerA_address.to_string());
  DEBUG("PeerB address: {}", peerB_address.to_string());

  int read_at_a = 0;
  int read_at_b = 0;

  auto conn_on_read_a =
      peerA.on_read.connect([&](const rtpmidid::packet_t &data,
                                const rtpmidid::network_address_t &c) {
        DEBUG("Got data on read {}, {} bytes", c.to_string(), data);
        std::string str = std::string((const char *)data.get_data());
        ASSERT_TRUE(str == "test data");
        auto hostname = get_hostname();
        ASSERT_TRUE(c.hostname() == hostname);
        ASSERT_TRUE(c.ip() == "127.0.0.2");
        ASSERT_TRUE(c.port() == 13002);

        read_at_a++;
      });
  auto conn_on_read_b =
      peerB.on_read.connect([&](const rtpmidid::packet_t &data,
                                const rtpmidid::network_address_t &c) {
        DEBUG("Got data on read {}, {} bytes", c.to_string(), data);
        std::string str = std::string((const char *)data.get_data());
        ASSERT_TRUE(str == "test data");
        ASSERT_TRUE(c.hostname() == "localhost");
        ASSERT_TRUE(c.ip() == "127.0.0.1");
        ASSERT_TRUE(c.port() == 13001);

        read_at_b++;
      });

  DEBUG("Peer ready");
  rtpmidid::io_bytes_writer_static<1500> data;
  data.write_str0("test data");
  rtpmidid::packet_t packet(data.start, data.size());
  peerA.sendto(packet, peerB_address);

  poller_wait_until([&]() { return read_at_b == 1; });
  ASSERT_EQUAL(read_at_b, 1);

  peerB.sendto(packet, peerA_address);

  poller_wait_until([&]() { return read_at_a == 1; });
  ASSERT_EQUAL(read_at_a, 1);

  peerB.sendto(packet, peerA_address);

  poller_wait_until([&]() { return read_at_a == 2; });
  ASSERT_EQUAL(read_at_a, 2);
}

static std::string get_hostname() {
  constexpr auto MAX_HOSTNAME_SIZE = 256;
  std::array<char, MAX_HOSTNAME_SIZE> hostname{0};
  hostname.fill(0);
  ::gethostname(hostname.data(), std::size(hostname));
  return std::string(hostname.data());
}

void test_client_state_machine() {
  rtpmidid::rtpclient_t client("Test");

  rtpmidid::udppeer_t peerA_control("localhost", "13001");
  rtpmidid::udppeer_t peerA_midi("localhost", "13002");

  bool got_control_connection = false;
  bool got_midi_connection = false;
  auto peerA_on_read_connection_control = peerA_control.on_read.connect(
      [&](const rtpmidid::packet_t &data,
          const rtpmidid::network_address_t &data_address) {
        auto packet_type = data.get_packet_type();

        DEBUG("Received a CONTROL {} packet", packet_type);
        if (packet_type == rtpmidid::packet_type_e::COMMAND) {
          rtpmidid::command_packet_t req(data);
          DEBUG("Packet: {}", req.to_string());
        }

        ASSERT_FALSE(got_control_connection);
        ASSERT_FALSE(got_midi_connection);
        DEBUG("Got data on read {}, {} bytes", data_address.to_string(), data);
        DEBUG("Control port is {}", client.control_address.port());
        ASSERT_TRUE(data_address.port() == client.local_base_port);

        ASSERT_TRUE(packet_type == rtpmidid::packet_type_e::COMMAND);

        rtpmidid::command_packet_t req(data);
        got_control_connection = req.is_command_packet();
        DEBUG("Packet: {}", req.to_string());

        std::array<uint8_t, 1500> buffer;
        rtpmidid::command_packet_t response(buffer.data(), buffer.size());
        response.initialize()
            .set_command(rtpmidid::command_e::OK)
            .set_sender_ssrc(0xBEEF)
            .set_initiator_token(req.get_initiator_token())
            .set_name("Manual packets");
        DEBUG("Response: {} {} bytes", response, response.get_size_to_send());
        ASSERT_GT(response.get_size_to_send(), 16);

        peerA_control.sendto(response.as_send_packet(), data_address);
      });

  auto peerA_on_read_connection_midi = peerA_midi.on_read.connect(
      [&](const rtpmidid::packet_t &data,
          const rtpmidid::network_address_t &data_address) {
        auto packet_type = data.get_packet_type();
        DEBUG("Received a MIDI {} packet", packet_type);

        ASSERT_FALSE(got_midi_connection);
        DEBUG("Got data on read {}, {} bytes", data_address.to_string(), data);
        DEBUG("MIDI port is {}", client.midi_address.port());
        ASSERT_TRUE(data_address.port() == client.local_base_port + 1);

        ASSERT_TRUE(packet_type == rtpmidid::packet_type_e::COMMAND);

        rtpmidid::command_packet_t req(data);
        DEBUG("Packet: {}", req.to_string());

        std::array<uint8_t, 1500> buffer;
        rtpmidid::command_packet_t response(buffer.data(), buffer.size());
        response.initialize()
            .set_command(rtpmidid::command_e::OK)
            .set_sender_ssrc(0xBEEF)
            .set_initiator_token(req.get_initiator_token())
            .set_name("Manual packets");

        DEBUG("Response: {} {} bytes", response, response.get_size_to_send());
        ASSERT_GT(response.get_size_to_send(), 16);
        peerA_midi.sendto(response.as_send_packet(), data_address);

        got_midi_connection = true;
      });

  client.add_server_address("localhost", "13001");
  client.connect();

  poller_wait_until([&]() { return got_control_connection; });
  ASSERT_TRUE(got_control_connection)
  poller_wait_until([&]() { return got_midi_connection; });
  ASSERT_TRUE(got_midi_connection)

  poller_wait_until([&]() {
    return client.state == rtpmidid::rtpclient_t::states_e::AllConnected;
  });

  client.send_ck0_with_timeout();

  // Wait for some CK
  bool received_ck_request = false;
  poller_wait_until([&]() { return received_ck_request; });
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_network_address_list),
      TEST(test_udppeer),
      TEST(test_client_state_machine),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

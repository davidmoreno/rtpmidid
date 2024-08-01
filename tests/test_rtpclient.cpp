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

void test_basic_packet() {
  std::array<uint8_t, 16> buffer;
  rtpmidid::packet_t package(buffer);

  package.set_uint8(0, 0x01);
  package.set_uint8(1, 0x02);
  package.set_uint16(2, 0x0304);
  package.set_uint16(4, 0x0506); // this should be overwritten
  package.set_uint32(4, 0x0708090A);
  package.set_uint64(8, 0x0B0C0D0E0F101112);
  DEBUG("Packet: {}", package.to_string());

  ASSERT_EQUAL(package.get_uint8(0), 0x01);
  ASSERT_EQUAL(package.get_uint8(1), 0x02);
  ASSERT_EQUAL(package.get_uint16(2), 0x0304);
  ASSERT_EQUAL(package.get_uint32(4), 0x0708090A);
  ASSERT_EQUAL(package.get_uint64(8), 0x0B0C0D0E0F101112);
}

void test_midi_packet() {
  auto midi_msg = hex_to_bin("80 61"
                             "0001"          // seq nr
                             "0000 1000"     // timestamp
                             "00 BE EF 00"   // dest SSRC -- will be changed
                             "03 90 60 7f"); // Lenght + data
  auto midi_packet = rtpmidid::packet_midi_t(midi_msg.start, midi_msg.size());

  DEBUG("MIDI PACKET: {}", midi_packet);

  ASSERT_EQUAL(midi_packet.get_ssrc(), 0x00BEEF00);
  ASSERT_EQUAL(midi_packet.get_timestamp(), 0x00001000);
  ASSERT_EQUAL(midi_packet.get_sequence_number(), 1);

  auto event_list = midi_packet.get_midi_events();
  DEBUG("Event list: {}", event_list.to_string());
  int count = 0;
  for (auto midi_event : event_list) {
    DEBUG("MIDI EVENT: {}", midi_event.to_string());
    count += 1;
  }
  ASSERT_EQUAL(count, 1);
}

void test_client_state_machine() {
  rtpmidid::rtpclient_t client("Test");

  rtpmidid::udppeer_t peerA_control("localhost", "13001");
  rtpmidid::udppeer_t peerA_midi("localhost", "13002");

  bool got_control_connection = false;
  bool got_midi_connection = false;
  bool received_ck_request = false;
  auto peerA_on_read_connection_control = peerA_control.on_read.connect(
      [&](const rtpmidid::packet_t &data,
          const rtpmidid::network_address_t &data_address) {
        auto packet_type = data.get_packet_type();

        DEBUG("Received a CONTROL {} packet", packet_type);
        if (packet_type == rtpmidid::packet_type_e::COMMAND) {
          rtpmidid::packet_command_t req(data);
          DEBUG("Packet: {}", req.to_string());
        }

        ASSERT_FALSE(got_control_connection);
        ASSERT_FALSE(got_midi_connection);
        DEBUG("Got data on read {}, {} bytes", data_address.to_string(), data);
        DEBUG("Control port is {}", client.control_address.port());
        ASSERT_TRUE(data_address.port() == client.local_base_port);

        ASSERT_TRUE(packet_type == rtpmidid::packet_type_e::COMMAND);

        rtpmidid::packet_command_in_ok_t req(data);
        got_control_connection = req.is_command_packet();
        DEBUG("Packet: {}", req.to_string());

        std::array<uint8_t, 1500> buffer;
        rtpmidid::packet_command_in_ok_t response(buffer.data(), buffer.size());
        response.initialize(rtpmidid::command_e::OK)
            .set_sender_ssrc(0xBEEF)
            .set_initiator_token(req.get_initiator_token())
            .set_name("Manual packets");
        DEBUG("Response: {} {} bytes", response.to_string(),
              response.get_size_to_send());
        ASSERT_GT(response.get_size_to_send(), 16);

        peerA_control.sendto(response.as_send_packet(), data_address);
      });

  auto peerA_on_read_connection_midi = peerA_midi.on_read.connect(
      [&](const rtpmidid::packet_t &data,
          const rtpmidid::network_address_t &data_address) {
        auto packet_type = data.get_packet_type();
        DEBUG("Received a MIDI {} packet", packet_type);

        ASSERT_EQUAL(packet_type, rtpmidid::packet_type_e::COMMAND);

        rtpmidid::packet_command_t req(data);
        DEBUG("Packet: {}", req.to_string());

        switch (req.get_command()) {
        case rtpmidid::command_e::IN: {
          ASSERT_FALSE(got_midi_connection);
          DEBUG("Got data on read {}, {} bytes", data_address.to_string(),
                data);
          DEBUG("MIDI port is {}", client.midi_address.port());
          ASSERT_TRUE(data_address.port() == client.local_base_port + 1);

          ASSERT_TRUE(packet_type == rtpmidid::packet_type_e::COMMAND);

          rtpmidid::packet_command_in_ok_t req(data);
          DEBUG("Packet: {}", req.to_string());

          std::array<uint8_t, 1500> buffer;
          rtpmidid::packet_command_in_ok_t response(buffer.data(),
                                                    buffer.size());
          response.initialize(rtpmidid::command_e::OK)
              .set_sender_ssrc(0xBEEF)
              .set_initiator_token(req.get_initiator_token())
              .set_name("Manual packets");

          DEBUG("Response: {} {} bytes", response.to_string(),
                response.get_size_to_send());
          ASSERT_GT(response.get_size_to_send(), 16);
          peerA_midi.sendto(response.as_send_packet(), data_address);

          got_midi_connection = true;
        } break;
        case rtpmidid::command_e::CK: {
          ASSERT_TRUE(got_midi_connection);
          ASSERT_TRUE(client.peer.status ==
                      rtpmidid::rtppeer_t::status_e::CONNECTED);
          rtpmidid::packet_command_ck_t req(data);
          DEBUG("Got ck packet: {}", req.to_string());
          std::array<uint8_t, 1500> buffer;
          rtpmidid::packet_command_ck_t response(buffer.data(), buffer.size());
          if (req.get_count() == 0) {
            response.initialize()
                .set_count(req.get_count() + 1)
                .set_sender_ssrc(0xBEEF)
                .set_ck0(req.get_ck0())
                .set_ck1(100);

            DEBUG("Send response: {} bytes", response.to_string());

            peerA_midi.sendto(response.as_send_packet(), data_address);
          } else {
            ASSERT_EQUAL(req.get_count(), 2);
            received_ck_request = true;
          }

        } break;
        default: {
          rtpmidid::packet_command_t req(data);
          FAIL(fmt::format("Unexpected command: {}", req.get_command()));
        }
        }
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
  poller_wait_until([&]() { return received_ck_request; });
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_network_address_list), TEST(test_udppeer),
      TEST(test_basic_packet),         TEST(test_midi_packet),
      TEST(test_client_state_machine),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

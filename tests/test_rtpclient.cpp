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
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpclient.hpp>
#include <rtpmidid/rtpserver.hpp>
#include <rtpmidid/udppeer.hpp>
#include <unistd.h>

static std::string get_hostname();

void test_udppeer() {
  rtpmidid::rtpclient_t client("Test");

  rtpmidid::udppeer_t peerA("localhost", "13001");
  rtpmidid::udppeer_t peerB("127.0.0.2", "13002");

  int read_at_a = 0;
  int read_at_b = 0;

  auto conn_on_read_a = peerA.on_read.connect(
      [&](rtpmidid::io_bytes_reader &data, const std::string &ip, int port) {
        DEBUG("Got data on read {}:{}, {} bytes", ip, port, data.size());
        std::string str = std::string(data.read_str0());
        ASSERT_TRUE(str == "test data");
        auto hostname = get_hostname();
        ASSERT_TRUE(ip == hostname);
        ASSERT_TRUE(port == 13002);

        read_at_a++;
      });
  auto conn_on_read_b = peerB.on_read.connect(
      [&](rtpmidid::io_bytes_reader &data, const std::string &ip, int port) {
        DEBUG("Got data on read {}:{}, {} bytes", ip, port, data.size());
        std::string str = std::string(data.read_str0());
        ASSERT_TRUE(str == "test data");
        ASSERT_TRUE(ip == "localhost");
        ASSERT_TRUE(port == 13001);

        read_at_b++;
      });

  DEBUG("Peer ready");
  rtpmidid::io_bytes_writer_static<1500> data;
  data.write_str0("test data");
  peerA.send(data, "127.0.0.2", "13002");

  poller_wait_until([&]() { return read_at_b == 1; });
  ASSERT_EQUAL(read_at_b, 1);

  peerB.send(data, "localhost", "13001");

  poller_wait_until([&]() { return read_at_a == 1; });
  ASSERT_EQUAL(read_at_a, 1);

  peerB.send(data, "localhost", "13001");

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

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_udppeer),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

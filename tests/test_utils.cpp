
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

#include "./test_utils.hpp"
#include "./test_case.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/poller.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

static int char_to_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + c - 'A';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + c - 'a';
  }
  ERROR("{} is not an HEX number", c);
  throw std::exception();
}

/**
 * @short Simple hex to bin
 *
 * Allows easy to understand hex to be converted to bin. Allows strings, binary
 * files and hex digits:
 *
 * Example:
 *
 * hex_to_bin(
 *  "[1000 0001] [0110 0001] 'SQ'"
 *  "00 00 00 00" // Timestamp
 *  "'BEEF'" // SSRC
 * );
 *
 * Bin must be 8 bits.
 */
rtpmidid::io_bytes_managed hex_to_bin(const std::string &str) {
  rtpmidid::io_bytes_managed buffer(
      str.length()); // max size. Normally around 1/2
  auto writer = rtpmidid::io_bytes_writer(buffer);

  // A state machine that alternates between most significant nibble, and least
  // significant nibble
  bool msn = false;
  bool quote = false;
  bool sqbr = false;
  int lastd = 0;
  uint8_t n = 0;
  for (char c : str) {
    if (quote) {
      if (c == '\'') {
        quote = false;
      } else {
        writer.write_uint8(c);
      }
    } else if (sqbr) {
      if (c == ']') {
        sqbr = false;
        writer.write_uint8(n);
      } else if (c == '0' || c == '1') {
        n <<= 1;
        n |= (c == '1');
      }

    } else if (c == '\'') {
      quote = true;
    } else if (c == '[') {
      sqbr = true;
      n = 0;
    } else if (!isalnum(c)) {
      // skip non alnum
      continue;
    } else if (!msn) {
      lastd = char_to_nibble(c) << 4;
      msn = true;
    } else {
      lastd |= char_to_nibble(c);
      writer.write_uint8(lastd);
      msn = false;
    }
  }

  buffer.end = writer.position;
  return buffer;
}

test_client_t::test_client_t(int local_port, int remote_port) {
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(local_port);
  if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
    throw rtpmidid::exception(
        "Can not open control socket. Maybe address is in use?");
  }
  socklen_t len = sizeof(servaddr);
  ::getsockname(sockfd, (struct sockaddr *)&servaddr, &len);
  this->local_port = htons(servaddr.sin_port);
  this->remote_port = remote_port;

  DEBUG("Test client port {}", this->local_port);
}

void test_client_t::send(rtpmidid::io_bytes_reader &&msg) {
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(remote_port);
  inet_aton("127.0.0.1", &servaddr.sin_addr);

  auto len = ::sendto(sockfd, msg.start, msg.size(), 0,
                      (struct sockaddr *)&servaddr, sizeof(servaddr));

  ASSERT_GTE(len, 0);
  ASSERT_EQUAL(static_cast<uint32_t>(len), msg.size());

  // After each send, process it
  rtpmidid::poller.wait();
}

void test_client_t::recv(rtpmidid::io_bytes_reader &&msg) {
  auto len = ::recv(sockfd, msg.start, msg.size(), 0);
  msg.end = msg.start + len;
  msg.position = msg.start;
}

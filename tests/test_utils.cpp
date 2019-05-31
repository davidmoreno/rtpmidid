
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "./test_utils.hpp"
#include "./test_case.hpp"

static int char_to_nibble(char c){
  if (c >= '0' && c <= '9'){
    return c - '0';
  }
  if (c >= 'A' && c <= 'F'){
    return 10 + c - 'A';
  }
  ERROR("{} is not an HEX number", c);
  throw std::exception();
}

managed_parse_buffer_t hex_to_bin(const std::string &str){
  managed_parse_buffer_t buffer(str.length()); // max size. Normally around 1/2

  // A state machine that alternates between most significant nibble, and least significant nibble
  bool msn = false;
  bool quote = false;
  int lastd = 0;
  for (char c: str){
    if (quote){
      if (c == '\''){
        quote = false;
      } else {
        buffer.buffer.write_uint8(c);
      }
    }
    else if (c == '\''){
      quote = true;
    }
    else if (!isalnum(c)) {
      // skip non alnum
      continue;
    }
    else if (!msn){
      lastd = char_to_nibble(c) << 4;
      msn = true;
    } else {
      lastd |= char_to_nibble(c);
      buffer.buffer.write_uint8(lastd);
      msn = false;
    }
  }

  // Revert to read mode
  buffer.buffer.end = buffer.buffer.position;
  buffer.buffer.position = buffer.buffer.start;

  // buffer.buffer.print_hex(true);

  return buffer;
}


test_client_t::test_client_t(int local_port, int remote_port){
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(local_port);
  if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
    throw rtpmidid::exception("Can not open control socket. Maybe address is in use?");
  }
  socklen_t len = sizeof(servaddr);
  ::getsockname(sockfd, (struct sockaddr*)&servaddr, &len);
  this->local_port = htons(servaddr.sin_port);
  this->remote_port = remote_port;

  DEBUG("Test client port {}", this->local_port);
}

void test_client_t::send(rtpmidid::parse_buffer_t &msg){
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(remote_port);
  inet_aton("127.0.0.1", &servaddr.sin_addr);

  auto len = ::sendto(sockfd, msg.start, msg.size(), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

  ASSERT_EQUAL(len, msg.size());
}

void test_client_t::recv(rtpmidid::parse_buffer_t &msg){
  auto len = ::recv(sockfd, msg.start, msg.capacity(), 0);
  msg.end = msg.start + len;
  msg.position = msg.start;
}

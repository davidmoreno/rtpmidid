/**
 * Real Time Protocol Music Industry Digital Interface Daemon
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "./mdns.hpp"
#include "./exceptions.hpp"
#include "./poller.hpp"
#include "./logger.hpp"


using namespace rtpmidid;

mdns::mdns(){
  struct sockaddr_in servaddr;
  socketfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketfd < 0){
    throw rtpmidid::exception("Can not open mDNS socket. Out of sockets?");
  }
  int c = 1;
  if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &c, sizeof(c)) < 0){
    throw rtpmidid::exception("Can not open mDNS socket. Address reuse denied? {}", strerror(errno));
  }
  c = 1;
  if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEPORT, &c, sizeof(c)) < 0){
    throw rtpmidid::exception("Can not open mDNS socket. Port reuse denied? {}", strerror(errno));
  }

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  inet_aton("224.0.0.251", &servaddr.sin_addr);
  servaddr.sin_port = htons(5353);
  if (bind(socketfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
    throw rtpmidid::exception("Can not open mDNS socket. Maybe addres is in use?");
  }
  poller.add_fd_in(socketfd, [this](int){ this->mdns_ready(); });

  DEBUG("mDNS wating for requests at 224.0.0.251:5353");
}

mdns::~mdns(){

}

void mdns::on_discovery(const std::string &service, std::function<void(const mdns::service &)> &){

}
void mdns::announce(const std::string &servicename){

}

uint32_t parse_uint32(const char *buffer){
  return ((uint32_t)buffer[0]<<24) + ((uint32_t)buffer[1]<<16) + ((uint32_t)buffer[2]<< 8) + ((uint32_t)buffer[3]);
}

uint16_t parse_uint16(const uint8_t *buffer){
  return ((uint16_t)buffer[0]<< 8) + ((uint16_t)buffer[1]);
}

void print_hex(const uint8_t *data, int n){
  for( int i=0 ; i<n ; i++ ){
    printf("%02X ", data[i] & 0x0FF);
    if (i % 4 == 3)
      printf(" ");
    if (i % 16 == 15)
      printf("\n");
  }
  printf("\n");
  for( int i=0 ; i<n ; i++ ){
    if (isalnum(data[i])){
      printf("%c", data[i]);
    }
    else{
      printf(".");
    }
    if (i % 4 == 3)
      printf(" ");
    if (i % 16 == 15)
      printf("\n");
  }
  printf("\n");
}

// Due on how labels are in mem, jsut change the separators into '.' until two 0 are found.
int read_label(uint8_t *start, uint8_t *end, uint8_t *base, char **str){
  uint8_t *data = start;
  while(data < end){
    uint8_t nchars = *data;
    if (nchars == 192){
      data++;
      if (base + *data > end){
        throw exception("Invalid package. Label pointer out of bounds.");
      }
      DEBUG("Pointer to {}: {}", *data, base + *data);
      *str = (char*)base + *data;
      return 2;
    }
    data++;
    if (nchars == 0){
      *str = (char*)start;
      return data - start;
    }
    *(data - 1)  = '.';
    data += nchars;
  }
  throw exception("Invalid package. Label out of bounds.");
}

void mdns::mdns_ready(){
  uint8_t buffer[1501];
  memset(buffer, 0, sizeof(buffer));
  struct sockaddr_in cliaddr;
  unsigned int len = 0;
  auto read_length = recvfrom(socketfd, buffer, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
  DEBUG("Got some data from mDNS: {}", read_length);

  print_hex(buffer, read_length);

  if (read_length > 1500){
    ERROR("This mDNS implementation is not prepared for packages longer than 1500 bytes. Please fill a bug report. Ignoring package.");
    return;
  }

  if (read_length < 16){
    ERROR("Invalid mDNS packet. Minimum size is 16 bytes. Ignoring.");
    return;
  }

  int tid = parse_uint16(buffer);
  bool is_query = !(buffer[2] & 8);
  int opcode = (buffer[2] >> 3) & 0x0F;
  int nquestions = parse_uint16(buffer+4);
  int nanswers = parse_uint16(buffer+6);
  int nauthority = parse_uint16(buffer+8);
  int nadditional = parse_uint16(buffer+10);

  DEBUG(
      "mDNS packet: id: {}, is_query: {}, opcode: {}, nquestions: {}, nanswers: {}, nauthority: {}, nadditional: {}",
      tid, is_query ? "true" : "false", opcode, nquestions, nanswers, nauthority, nadditional
  );

  int n;
  uint8_t *data = buffer + 12;
  uint8_t *end = buffer + read_length;
  char *label;
  for (n=0; n<nquestions; n++){
    len = read_label(data, end, buffer, &label);
    data += len;
    if (data + 4 > end){
      throw exception("Invalid package");
    }
    int type_ = parse_uint16(data);
    int class_ = parse_uint16(data+2);
    DEBUG("Question about: {} {} {}", label, type_, class_);
    data += 4;
  }
}

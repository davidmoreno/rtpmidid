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
#include <netinet/in.h>

#include "./mdns.hpp"
#include "./exceptions.hpp"
#include "./poller.hpp"
#include "./logger.hpp"

const auto DEBUG0 = false;


using namespace rtpmidid;

mdns::mdns(){
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

  memset(&multicast_addr, 0, sizeof(multicast_addr));
  multicast_addr.sin_family = AF_INET;
  inet_aton("224.0.0.251", &multicast_addr.sin_addr);
  multicast_addr.sin_port = htons(5353);
  if (bind(socketfd, (const struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0){
    throw rtpmidid::exception("Can not open mDNS socket. Maybe addres is in use?");
  }
  poller.add_fd_in(socketfd, [this](int){ this->mdns_ready(); });

  DEBUG("mDNS wating for requests at 224.0.0.251:5353");
}

mdns::~mdns(){

}

uint32_t parse_uint32(const uint8_t *buffer){
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
    if (isprint(data[i])){
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
int read_label(uint8_t *start, uint8_t *end, uint8_t *base, char *str, char *str_end){
  // DEBUG(
  //   "Read label start: {:p}, end: {:p}, base: {:p}, str: {:p}, str_end: {:p}",
  //   start, end, base, str, str_end
  // );
  uint8_t *data = start;
  bool first = true;
  while(data < end && str < str_end){
    uint8_t nchars = *data;
    if (nchars == 192){
      data++;
      if (base + *data > start){
        throw exception("Invalid package. Label pointer out of bounds. Max pos is begining current record.");
      }
      if (first)
        first = false;
      else
        *str++ = '.';
      *str = 0;
      auto nbytes = (data - start);
      // DEBUG("Label is compressed, refers to {}. So far read: {} bytes, <{}>", *data, nbytes, str - nbytes);
      read_label(base + *data, end, base, str, str_end);
      return nbytes + 2;
    }
    data++;
    if (nchars == 0){
      *str = 0;
      return data - start;
    }
    if (first)
      first = false;
    else
      *str++ = '.';
    for (int i=0; i< nchars; i++){
      *str++ = *data++;
    }
  }
  throw exception("Invalid package. Label out of bounds.");
}

uint8_t *read_question(uint8_t *buffer, uint8_t *end, uint8_t *data){
  char label[128];

  auto len = read_label(data, end, buffer, label, label + sizeof(label));
  data += len;
  if (data + 4 > end){
    throw exception("Invalid package");
  }
  int type_ = parse_uint16(data);
  int class_ = parse_uint16(data+2);
  DEBUG("Question about: {} {} {}. I dont answer yet.", label, type_, class_);
  data += 4;

  return data;
}

uint8_t *read_answer(mdns *server, uint8_t *buffer, uint8_t *end, uint8_t *data){
  char label[128];

  auto len = read_label(data, end, buffer, label, label + sizeof(label));
  data += len;
  if (data + 4 > end){
    throw exception("Invalid package");
  }
  auto type_ = parse_uint16(data);
  auto class_ = parse_uint16(data+2);
  data += 4;

  // auto ttl = parse_uint32(data);
  data += 4;

  auto data_length = parse_uint16(data);
  data += 2;

  if (type_ == mdns::PTR){ // PTR
    char answer[128];
    len = read_label(data, end, buffer, answer, answer + sizeof(answer));
    DEBUG("PTR Answer about: {} {} {} -> <{}>", label, type_, class_, answer);
    DEBUG("Asking now about {} SRV", answer);
    server->query(answer, mdns::SRV);
  }
  else if (type_ == mdns::SRV){ // PTR
    // auto priority = parse_uint16(data);
    data += 2;
    // auto weight = parse_uint16(data);
    data += 2;
    auto port = parse_uint16(data);
    data += 2;

    char target[128];
    len = read_label(data, end, buffer, target, target + sizeof(target));

    DEBUG("Found {} at {}:{}", label, target, port);
    server->detected_service(label, target, port);

    // char answer[128];
    // len = read_label(data, end, buffer, answer, answer + sizeof(answer));
    // DEBUG("PTR Answer about: {} {} {} -> <{}>", label, type_, class_, answer);
    // DEBUG("Asking now about {} SRV", answer);
    // server->query(answer, mdns::SRV);
  }
  else{
    DEBUG("I dont know how to manage DNS record type type {}.", type_);
  }
  data += data_length;

  return data;
}

void mdns::on_discovery(const std::string &service, std::function<void(const mdns::service &)> f){
  if (service.length() > 100){
    throw exception("Service name too long. I only know how to search for smaller names.");
  }
  discovery_map[service].push_back(f);

  query(service, PTR);
}

void mdns::query(const std::string &name, mdns::query_type_e type){
  // Now I will ask for it
  // I will prepare the package here
  uint8_t packet[120];
  // transaction id. always 0 for mDNS
  memset(packet, 0, sizeof(packet));
  // I will only set what I need.
  packet[4] = 0;
  packet[5] = 1;
  // Now the query itself
  uint8_t *data = packet + 12;
  auto strI = name.begin();
  auto endI = name.end();
  for(auto I=strI; I < endI; ++I){
    if (*I == '.'){
      *data++ = I - strI;
      for( ; strI<I ; ++strI ){
        *data++ = *strI;
      }
      strI++;
    }
  }
  *data++ = endI - strI;
  for( ; strI<endI ; ++strI ){
    *data++ = *strI;
  }
  // end of labels
  *data++ = 0;
  // type ptr
  *data++ = ( type >> 8 ) & 0x0FF;
  *data++ = type & 0x0FF;
  // query
  *data++ = 0;
  *data++ = 0x01;

  /// DONE
  if (DEBUG0){
    DEBUG("Packet ready! {} bytes", data - packet);
    print_hex(packet, data - packet);
  }

  sendto(socketfd, packet, data - packet, MSG_CONFIRM, (const struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
}

void mdns::announce(const std::string &servicename){

}


void mdns::mdns_ready(){
  uint8_t buffer[1501];
  memset(buffer, 0, sizeof(buffer));
  struct sockaddr_in cliaddr;
  unsigned int len = 0;
  auto read_length = recvfrom(socketfd, buffer, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);

  if (DEBUG0){
    DEBUG("Got some data from mDNS: {}", read_length);
    print_hex(buffer, read_length);
  }
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
  auto nquestions = parse_uint16(buffer+4);
  auto nanswers = parse_uint16(buffer+6);
  auto nauthority = parse_uint16(buffer+8);
  auto nadditional = parse_uint16(buffer+10);

  if (DEBUG0){
    DEBUG(
        "mDNS packet: id: {}, is_query: {}, opcode: {}, nquestions: {}, nanswers: {}, nauthority: {}, nadditional: {}",
        tid, is_query ? "true" : "false", opcode, nquestions, nanswers, nauthority, nadditional
    );
  }
  uint32_t i;
  uint8_t *buffer_end = buffer + read_length;
  uint8_t *data = buffer+12;
  for ( i=0 ; i <nquestions ; i++ ){
      data = read_question(buffer, buffer_end, data);
      if (!data){
        WARNING("Ignoring mDNS packet!");
        return;
      }
  }
  for ( i=0 ; i <nanswers ; i++ ){
      data = read_answer(this, buffer, buffer_end, data);
      if (!data){
        WARNING("Ignoring mDNS packet!");
        return;
      }
  }
}

bool endswith(std::string_view const &full, std::string_view const &ending){
    if (full.length() >= ending.length()) {
        return (0 == full.compare (full.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

void mdns::detected_service(const std::string_view &service, const std::string_view &hostname, uint16_t port){
  for(auto &kv: discovery_map){
    if (endswith(service, kv.first)){
      auto service_s = mdns::service{
        std::string(service), std::string(hostname), port
      };
      for (auto &f: kv.second){
        f(service_s);
      }
    }
  }
}


std::string std::to_string(const rtpmidid::mdns::service &s){
  return fmt::format("service: {}, hostname: {}, port: {}", s.service, s.hostname, s.port);
}

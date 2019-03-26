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
#include "./netutils.hpp"

const auto DEBUG0 = true;


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

bool read_question(mdns *server, parse_buffer_t &buffer){
  uint8_t label[128];
  parse_buffer_t parse_label = {label, label+sizeof(label), label};

  read_label(buffer, parse_label);
  int type_ = buffer.read_uint16();
  int class_ = buffer.read_uint16();
  DEBUG("Question about: {} {} {}.", label, type_, class_);

  return server->answer_if_known(mdns::query_type_e(type_), (char*)label);
}

bool read_answer(mdns *server, parse_buffer_t &buffer){
  uint8_t label[128];
  parse_buffer_t buffer_label = {
    label,
    label + 128,
    label
  };

  read_label(buffer, buffer_label);
  auto type_ = buffer.read_uint16();
  auto class_ = buffer.read_uint16();

  // auto ttl = read_uint32(data);
  buffer.position += 4;

  auto data_length = buffer.read_uint16();
  auto *pos = buffer.position;

  if (type_ == mdns::PTR){ // PTR
    uint8_t answer[128];
    parse_buffer_t buffer_answer = {
      answer,
      answer+128,
      answer
    };
    read_label(buffer, buffer_answer);
    DEBUG("PTR Answer about: {} {} {} -> <{}>", label, type_, class_, answer);
    DEBUG("Asking now about {} SRV", answer);
    mdns::service_srv service = {
      (char*)label,
      mdns::PTR,
      (char*)answer,
    };
    server->detected_service(&service);
  }
  else if (type_ == mdns::SRV){ // PTR
    // auto priority = read_uint16(data);
    buffer.position += 2;
    // auto weight = read_uint16(data);
    buffer.position += 2;
    buffer.assert_valid_position();
    auto port = buffer.read_uint16();

    uint8_t target[128];
    parse_buffer_t buffer_target = {
      target,
      target+128,
      target
    };
    read_label(buffer, buffer_target);

    mdns::service_srv service = {
      (char*)label,
      mdns::SRV,
      (char*)target,
      port
    };
    server->detected_service(&service);

    // char answer[128];
    // len = read_label(data, end, buffer, answer, answer + sizeof(answer));
    // DEBUG("PTR Answer about: {} {} {} -> <{}>", label, type_, class_, answer);
    // DEBUG("Asking now about {} SRV", answer);
    // server->query(answer, mdns::SRV);
  }
  else if (type_ == mdns::A){
    mdns::service_a service = {
      (char*)label,
      mdns::A,
    };
    service.ip[0] = buffer.read_uint8();
    service.ip[1] = buffer.read_uint8();
    service.ip[2] = buffer.read_uint8();
    service.ip[3] = buffer.read_uint8();

    server->detected_service(&service);

  }
  buffer.position = pos + data_length;
  buffer.assert_valid_position();

  return true;
}

void mdns::on_discovery(const std::string &service, mdns::query_type_e qt, std::function<void(const mdns::service *)> f){
  if (service.length() > 100){
    throw exception("Service name too long. I only know how to search for smaller names.");
  }
  discovery_map[std::make_pair(qt, service)].push_back(f);

  query(service, qt);
}

void mdns::query(const std::string &service, mdns::query_type_e qt, std::function<void(const mdns::service *)> f){
  if (service.length() > 100){
    throw exception("Service name too long. I only know how to search for smaller names.");
  }
  query_map[std::make_pair(qt, service)].push_back(f);

  query(service, qt);
}


void mdns::announce(std::unique_ptr<service> service, bool broadcast){
  if (service->label.length()>100){
    throw exception("Cant announce a service this long. Max size is 100 chars.");
  }
  auto idx = std::make_pair(service->type, service->label);

  // preemptively tell everybody
  if (broadcast){
    send_response(*service);
    DEBUG("Announce service: {}", service->label);
  }

  // And store. This order to use service before storing.
  announcements[idx].push_back(std::move(service));
}

void mdns::send_response(const service &service){
  uint8_t packet[1500];
  memset(packet, 0, sizeof(packet));
  parse_buffer_t buffer = { packet, packet + 1500, packet };

  // Response and authoritative
  buffer.position[2] = 0x84;

  buffer.position += 6;
  // One answer
  buffer.write_uint16(1);

  // The query
  buffer.position = buffer.start + 12;
  write_label(buffer, service.label);

  // type
  buffer.write_uint16(service.type);
  // class IN
  buffer.write_uint16(1);
  // ttl
  buffer.write_uint32(600); // FIXME should not be fixed.
  // data_length. I prepare the spot
  auto length_data_pos = buffer.position;
  buffer.position += 2;
  switch(service.type){
    case mdns::A:{
      auto a = static_cast<const mdns::service_a*>(&service);
      buffer.write_uint8(a->ip[0]);
      buffer.write_uint8(a->ip[1]);
      buffer.write_uint8(a->ip[2]);
      buffer.write_uint8(a->ip[3]);
    }
    break;
    case mdns::PTR:{
      auto ptr = static_cast<const mdns::service_ptr*>(&service);
      write_label(buffer, ptr->servicename);
    }
    break;
    case mdns::SRV:{
      auto srv = static_cast<const mdns::service_srv*>(&service);
      buffer.write_uint16(0); // priority
      buffer.write_uint16(0); // weight
      buffer.write_uint16(srv->port);
      write_label(buffer, srv->hostname);
    }
    break;
    default:
      throw exception("I dont know how to announce this mDNS answer type: {}", service.type);
  }

  uint16_t nbytes = buffer.position - length_data_pos - 2;
  DEBUG("Send RR type: {} size: {}", service.type, nbytes);

  // A little go and back
  raw_write_uint16(length_data_pos, nbytes);

  sendto(socketfd, packet, buffer.length(), MSG_CONFIRM, (const struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
}


bool mdns::answer_if_known(mdns::query_type_e type_, const std::string &label){
  auto found = announcements.find(std::make_pair(type_, label));
  if (found != announcements.end()){
    for(auto &response: found->second){
      send_response(*response);
    }
    return true;
  }
  return false;
}

void mdns::query(const std::string &name, mdns::query_type_e type){
  // Now I will ask for it
  // I will prepare the package here
  uint8_t packet[120];
  // transaction id. always 0 for mDNS
  memset(packet, 0, sizeof(packet));
  // I will only set what I need.
  packet[5] = 1;
  // Now the query itself
  parse_buffer_t buffer = {packet, packet + 120, packet + 12};
  write_label(buffer, name);
  // type ptr
  buffer.write_uint16(type);
  // query
  buffer.write_uint16(1);

  /// DONE
  if (DEBUG0){
    DEBUG("Packet ready! {} bytes", buffer.length());
    buffer.print_hex();
  }

  sendto(socketfd, packet, buffer.length(), MSG_CONFIRM, (const struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
}

void parse_packet(mdns *mdns, parse_buffer_t &parse_buffer){
  int tid = parse_buffer.read_uint16();
  int8_t flags = parse_buffer.read_uint16();
  bool is_query = !(flags & 0x8000);
  int opcode = (flags >> 11) & 0x0007;
  auto nquestions = parse_buffer.read_uint16();
  auto nanswers = parse_buffer.read_uint16();
  auto nauthority = parse_buffer.read_uint16();
  auto nadditional = parse_buffer.read_uint16();

  if (DEBUG0){
    DEBUG(
        "mDNS packet: id: {}, is_query: {}, opcode: {}, nquestions: {}, nanswers: {}, nauthority: {}, nadditional: {}",
        tid, is_query ? "true" : "false", opcode, nquestions, nanswers, nauthority, nadditional
    );
  }
  uint32_t i;
  for ( i=0 ; i <nquestions ; i++ ){
      auto ok = read_question(mdns, parse_buffer);
      if (!ok){
        WARNING("Ignoring mDNS packet!");
        return;
      }
  }
  for ( i=0 ; i <nanswers ; i++ ){
      auto ok = read_answer(mdns, parse_buffer);
      if (!ok){
        WARNING("Ignoring mDNS packet!");
        return;
      }
  }
}

void mdns::mdns_ready(){
  uint8_t buffer[1501];
  memset(buffer, 0, sizeof(buffer));
  struct sockaddr_in cliaddr;
  unsigned int len = 0;
  auto read_length = recvfrom(socketfd, buffer, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);

  if (read_length < 16){
    ERROR("Invalid mDNS packet. Minimum size is 16 bytes. Ignoring.");
    return;
  }

  parse_buffer_t parse_buffer{
    buffer,
    buffer + read_length,
    buffer,
  };

  if (DEBUG0){
    DEBUG("Got some data from mDNS: {}", read_length);
    parse_buffer.print_hex(true);
  }
  if (read_length > 1500){
    ERROR("This mDNS implementation is not prepared for packages longer than 1500 bytes. Please fill a bug report. Ignoring package. Actually we never read more. FILL A BUG REPORT.");
    return;
  }

  parse_packet(this, parse_buffer);
}

bool endswith(std::string_view const &full, std::string_view const &ending){
    if (full.length() >= ending.length()) {
        return (0 == full.compare (full.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

void mdns::detected_service(const mdns::service *service){
  auto type_label = std::make_pair(service->type, service->label);

  for(auto &f: discovery_map[type_label]){
    f(service);
  }
  for(auto &f: query_map[type_label]){
    f(service);
  }

  // remove them from query map, as fulfilled
  query_map.erase(type_label);
}


std::string std::to_string(const rtpmidid::mdns::service_ptr &s){
  return fmt::format("PTR record. label: {}, pointer: {}", s.label, s.servicename);
}
std::string std::to_string(const rtpmidid::mdns::service_a &s){
  return fmt::format("A record. label: {}, ip: {}.{}.{}.{}", s.label, uint8_t(s.ip[0]), uint8_t(s.ip[1]), uint8_t(s.ip[2]), uint8_t(s.ip[3]));
}
std::string std::to_string(const rtpmidid::mdns::service_srv &s){
  return fmt::format("SRV record. label: {}, hostname: {}, port: {}", s.label, s.hostname, s.port);
}

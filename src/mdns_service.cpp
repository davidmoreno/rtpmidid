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
#include "./mdns.hpp"

using namespace rtpmidid;

mdns::service::service() {
}
mdns::service::service(std::string label_, query_type_e type_, uint32_t ttl_) :
  label(std::move(label_)), type(type_), ttl(ttl_) {
}
std::unique_ptr<mdns::service> mdns::service::clone() const{
  throw exception("Not implemented clone for basic service.");
}
bool mdns::service::equal(const mdns::service *other) {
  return false;
}
bool mdns::service::base_equal(const service *other){
  return type == other->type && label == other->label;
}
std::string mdns::service::to_string() const{
  return fmt::format("?? Q {} T {} ttl {}", label, type, ttl);
}


mdns::service_a::service_a() {}
mdns::service_a::service_a(std::string label_, uint32_t ttl_, uint8_t ip_[4]) :
  service(std::move(label_), A, ttl_) {
    ip[0] = ip_[0];
    ip[1] = ip_[1];
    ip[2] = ip_[2];
    ip[3] = ip_[3];
  }
std::unique_ptr<mdns::service_a::service> mdns::service_a::clone() const{
  auto ret = std::make_unique<mdns::service_a>();

  ret->label = label;
  ret->type = type;
  ret->ttl = ttl;
  ret->ip4 = ip4;

  return ret;
}
bool mdns::service_a::equal(const mdns::service_a::service *other_){
  if (!base_equal(other_))
    return false;
  if (const service_a *other = dynamic_cast<const service_a *>(other_)){
    return ip4 == other->ip4;
  }
  return false;
}
std::string mdns::service_a::to_string() const{
  return fmt::format("A Q {} T {} ttl {} ip {}.{}.{}.{}", label, type, ttl,
    uint8_t(ip[0]), uint8_t(ip[1]), uint8_t(ip[2]), uint8_t(ip[3]));
}


mdns::service_srv::service_srv() {}
mdns::service_srv::service_srv(std::string label_, uint32_t ttl_, std::string hostname_, uint16_t port_) :
  service(std::move(label_), SRV, ttl_),  hostname(std::move(hostname_)), port(port_) {}
std::unique_ptr<mdns::service_srv::service> mdns::service_srv::clone() const{
  auto ret = std::make_unique<mdns::service_srv>();

  ret->label = label;
  ret->type = type;
  ret->ttl = ttl;
  ret->hostname = hostname;
  ret->port = port;

  return ret;
}
bool mdns::service_srv::equal(const mdns::service_srv::service *other_){
  if (!base_equal(other_))
    return false;
  if (const service_srv *other = dynamic_cast<const service_srv *>(other_)){
    return hostname == other->hostname && port == other->port;
  }
  return false;
}
std::string mdns::service_srv::to_string() const{
  return fmt::format("SRV Q {} T {} ttl {} hostname {} port {}", label, type, ttl,
    hostname, port);
}


mdns::service_ptr::service_ptr() {}
mdns::service_ptr::service_ptr(std::string label_, uint32_t ttl_, std::string servicename_) :
  service(std::move(label_), PTR, ttl_), servicename(std::move(servicename_)) {}
std::unique_ptr<mdns::service_ptr::service> mdns::service_ptr::clone() const{
  auto ret = std::make_unique<mdns::service_ptr>();

  ret->label = label;
  ret->type = type;
  ret->ttl = ttl;
  ret->servicename = servicename;

  return ret;
}
bool mdns::service_ptr::equal(const mdns::service_ptr::service *other_){
  if (!base_equal(other_))
    return false;
  if (const service_ptr *other = dynamic_cast<const service_ptr *>(other_)){
    return servicename == other->servicename;
  }
  return false;
}
std::string mdns::service_ptr::to_string() const{
  return fmt::format("PTR Q {} T {} ttl {} servicename {}", label, type, ttl,
    servicename);
}

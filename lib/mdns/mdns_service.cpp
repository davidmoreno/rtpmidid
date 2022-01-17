/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2021 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "./mdns.hpp"


mdns::mdns::service::service() {}
mdns::mdns::service::service(std::string label_, mdns::mdns::query_type_e type_, uint32_t ttl_)
    : label(std::move(label_)), type(type_), ttl(ttl_) {}
std::unique_ptr<mdns::mdns::service> mdns::mdns::service::clone() const {
  throw rtpmidid::exception("Not implemented clone for basic service.");
}
bool mdns::mdns::service::equal(const mdns::mdns::service *other) { return false; }
bool mdns::mdns::service::base_equal(const service *other) {
  return type == other->type && label == other->label;
}
std::string mdns::mdns::service::to_string() const {
  return fmt::format("?? Q {} T {} ttl {}", label, type, ttl);
}

mdns::mdns::service_a::service_a() { type = A; }
mdns::mdns::service_a::service_a(std::string label_, uint32_t ttl_, uint8_t ip_[4])
    : service(std::move(label_), A, ttl_) {
  ip[0] = ip_[0];
  ip[1] = ip_[1];
  ip[2] = ip_[2];
  ip[3] = ip_[3];
}
std::unique_ptr<mdns::mdns::service_a::service> mdns::mdns::service_a::clone() const {
  auto ret = std::make_unique<mdns::mdns::service_a>();

  ret->label = label;
  ret->type = type;
  ret->ttl = ttl;
  ret->ip4 = ip4;

  return ret;
}
bool mdns::mdns::service_a::equal(const mdns::mdns::service_a::service *other_) {
  if (!base_equal(other_))
    return false;
  if (const service_a *other = dynamic_cast<const service_a *>(other_)) {
    return ip4 == other->ip4;
  }
  return false;
}
std::string mdns::mdns::service_a::to_string() const {
  return fmt::format("A Q {} T {} ttl {} ip {}.{}.{}.{}", label, type, ttl,
                     uint8_t(ip[0]), uint8_t(ip[1]), uint8_t(ip[2]),
                     uint8_t(ip[3]));
}

mdns::mdns::service_srv::service_srv() { type = SRV; }
mdns::mdns::service_srv::service_srv(std::string label_, uint32_t ttl_,
                               std::string hostname_, uint16_t port_)
    : service(std::move(label_), SRV, ttl_), hostname(std::move(hostname_)),
      port(port_) {}
std::unique_ptr<mdns::mdns::service_srv::service> mdns::mdns::service_srv::clone() const {
  auto ret = std::make_unique<mdns::mdns::service_srv>();

  ret->label = label;
  ret->type = type;
  ret->ttl = ttl;
  ret->hostname = hostname;
  ret->port = port;

  return ret;
}
bool mdns::mdns::service_srv::equal(const mdns::mdns::service_srv::service *other_) {
  if (!base_equal(other_))
    return false;
  if (const service_srv *other = dynamic_cast<const service_srv *>(other_)) {
    return hostname == other->hostname && port == other->port;
  }
  return false;
}
std::string mdns::mdns::service_srv::to_string() const {
  return fmt::format("SRV Q {} T {} ttl {} hostname {} port {}", label, type,
                     ttl, hostname, port);
}

mdns::mdns::service_ptr::service_ptr() { type = PTR; }
mdns::mdns::service_ptr::service_ptr(std::string label_, uint32_t ttl_,
                               std::string servicename_)
    : service(std::move(label_), PTR, ttl_),
      servicename(std::move(servicename_)) {}
std::unique_ptr<mdns::mdns::service_ptr::service> mdns::mdns::service_ptr::clone() const {
  auto ret = std::make_unique<mdns::mdns::service_ptr>();

  ret->label = label;
  ret->type = type;
  ret->ttl = ttl;
  ret->servicename = servicename;

  return ret;
}
bool mdns::mdns::service_ptr::equal(const mdns::mdns::service_ptr::service *other_) {
  if (!base_equal(other_))
    return false;
  if (const service_ptr *other = dynamic_cast<const service_ptr *>(other_)) {
    return servicename == other->servicename;
  }
  return false;
}
std::string mdns::mdns::service_ptr::to_string() const {
  return fmt::format("PTR Q {} T {} ttl {} servicename {}", label, type, ttl,
                     servicename);
}

mdns::mdns::service_txt::service_txt() { type = TXT; }
mdns::mdns::service_txt::service_txt(std::string label_, uint32_t ttl_,
                               std::string txt_)
    : service(std::move(label_), TXT, ttl_), txt(std::move(txt_)) {}
std::unique_ptr<mdns::mdns::service_txt::service> mdns::mdns::service_txt::clone() const {
  auto ret = std::make_unique<mdns::mdns::service_txt>();

  ret->label = label;
  ret->type = type;
  ret->ttl = ttl;
  ret->txt = txt;

  return ret;
}
bool mdns::mdns::service_txt::equal(const mdns::mdns::service_txt::service *other_) {
  if (!base_equal(other_))
    return false;
  if (const service_txt *other = dynamic_cast<const service_txt *>(other_)) {
    return txt == other->txt;
  }
  return false;
}
std::string mdns::mdns::service_txt::to_string() const {
  return fmt::format("TXT Q {} T {} ttl {} txt {}", label, type, ttl, txt);
}

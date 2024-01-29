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

#include "hwautoannounce.hpp"
#include "aseq.hpp"
#include <rtpmidid/logger.hpp>

namespace rtpmididns {

HwAutoAnnounce::HwAutoAnnounce(std::shared_ptr<aseq_t> aseq) {

  auto ann_port = aseq->create_port("Announcements", false);
  aseq->connect(aseq_t::port_t{0, 1},
                aseq_t::port_t{aseq->client_id, ann_port});

  aseq->for_devices([&](uint8_t device_id, const std::string &device_name,
                        aseq_t::client_type_e type) {
    if (type != aseq_t::client_type_e::TYPE_HARDWARE)
      return;
    aseq->for_ports(
        device_id, [&](uint8_t port_id, const std::string &port_name) {
          // DEBUG("HwAutoAnnounce::HwAutoAnnounce::for_ports {}:{} {}:{}
          // ",
          //       device_name, port_name, device_id, port_id);
          new_client_announcement(device_name, type,
                                  aseq_t::port_t{device_id, port_id});
        });
  });

  new_client_announcement_connection = aseq->new_client_announcement.connect(
      [this](const std::string &name, aseq_t::client_type_e type,
             const aseq_t::port_t &port) {
        // DEBUG("HwAutoAnnounce::HwAutoAnnounce::new_client_announcement {} {}
        // ",
        //       name, type);
        new_client_announcement(name, type, port);
      });
}

HwAutoAnnounce::~HwAutoAnnounce() {}

void HwAutoAnnounce::new_client_announcement(const std::string &name,
                                             aseq_t::client_type_e type,
                                             const aseq_t::port_t &port) {
  DEBUG("HwAutoAnnounce::new_client_announcement {} {} {}", name, type, port);
}

} // namespace rtpmididns
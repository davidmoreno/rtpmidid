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
#include "local_alsa_multi_listener.hpp"
#include "midirouter.hpp"
#include "settings.hpp"
#include <regex>
#include <rtpmidid/logger.hpp>

namespace rtpmididns {

HwAutoAnnounce::HwAutoAnnounce(std::shared_ptr<aseq_t> aseq,
                               std::shared_ptr<midirouter_t> router)
    : router(router), aseq(aseq) {

  auto ann_port = aseq->create_port("Announcements", false);
  auto conn = aseq->connect(aseq_t::port_t{0, 1},
                            aseq_t::port_t{aseq->client_id, ann_port});
  connections.push_back(std::move(conn));

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
  //  DEBUG("HwAutoAnnounce::new_client_announcement {} {} {}", name, type,
  //  port);
  auto ahwaa = &settings.alsa_hw_auto_export;

  if (ahwaa->type == settings_t::alsa_hw_auto_export_type_e::NONE)
    return;

  // Check positive regex
  if (!ahwaa->name_negative_regex.has_value())
    return;

  auto pos_regex = ahwaa->name_positive_regex.value();

  if (!std::regex_match(name, pos_regex)) {
    return;
  }

  // Check negative regex
  if (ahwaa->name_positive_regex.has_value()) {
    auto neg_regex = ahwaa->name_negative_regex.value();

    if (std::regex_match(name, neg_regex)) {
      return;
    }
  }

  // Check the type is appropiate
  if (ahwaa->type == settings_t::alsa_hw_auto_export_type_e::HARDWARE) {
    if (type != aseq_t::client_type_e::TYPE_HARDWARE)
      return;
  } else if (ahwaa->type == settings_t::alsa_hw_auto_export_type_e::SOFTWARE) {
    if (type != aseq_t::client_type_e::TYPE_SOFTWARE)
      return;
  } else if (ahwaa->type == settings_t::alsa_hw_auto_export_type_e::SYSTEM) {
    if (type != aseq_t::client_type_e::TYPE_SYSTEM)
      return;
  }

  DEBUG("HwAutoAnnounce::new_client_announcement {} {} {}", name, type, port);

  // Find the local_alsa_listener_t
  bool connected = false;
  router->for_each_peer<local_alsa_multi_listener_t>(
      [&](local_alsa_multi_listener_t *peer) {
        INFO("Auto announcing {} {} {}", name, type, port);
        auto annport = aseq_t::port_t{aseq->client_id, peer->port};
        auto con1 = aseq->connect(port, annport);
        auto con2 = aseq->connect(annport, port);
        // FIXME should be removed from here, or it may grow for each new
        // client. There is no real problem as will never disconnect, but it is
        // not clean.
        connections.push_back(std::move(con1));
        connections.push_back(std::move(con2));

        connected = true;
      });
  if (!connected) {
    ERROR("No local_alsa_multi_listener_t found to connect {} {} {}", name,
          type, port);
  }
}

} // namespace rtpmididns

/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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

#pragma once

#include <cstdint>
#include <memory>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtppeer.hpp>
#include <rtpmidid/rtpserverpeer.hpp>
#include <rtpmidid/signal.hpp>
#include <rtpmidid/udppeer.hpp>

namespace rtpmidid {
class rtpserver_t {
  NON_COPYABLE_NOR_MOVABLE(rtpserver_t)
public:
  // Callbacks to call when new connections
  signal_t<std::shared_ptr<rtppeer_t>> connected_event;
  signal_t<const io_bytes_reader &> midi_event;

  int max_peer_data_id = 1;
  std::vector<rtpserverpeer_t> peers;

  std::string name;
  udppeer_t control;
  udppeer_t midi;
  udppeer_t::on_read_t::connection_t on_read_control;
  udppeer_t::on_read_t::connection_t on_read_midi;

  rtpserver_t(std::string name, const std::string &port);
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ~rtpserver_t();

  int port() { return control.get_address().port(); }

  void create_peer_from(io_bytes_reader &&buffer, const network_address_t &addr,
                        rtppeer_t::port_e port);
  void remove_peer(int peer_id);

  void send_midi_to_all_peers(const io_bytes_reader &bufer);

  void data_ready(const io_bytes_reader &data, const network_address_t &addr,
                  rtppeer_t::port_e port);
  void sendto(const io_bytes_reader &b, rtppeer_t::port_e port,
              network_address_t &address, int remote_base_port);

  rtpserverpeer_t *find_peer_data_by_id(int id);
  rtpserverpeer_t *find_peer_by_packet(io_bytes_reader &b,
                                       rtppeer_t::port_e port);
  rtpserverpeer_t *find_peer_by_initiator_id(uint32_t initiator_id);
  rtpserverpeer_t *find_peer_by_ssrc(uint32_t ssrc);
};
} // namespace rtpmidid

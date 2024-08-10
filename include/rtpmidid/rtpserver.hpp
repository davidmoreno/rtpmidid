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
  struct peer_data_t {
    int id;
    std::shared_ptr<rtppeer_t> peer;

    rtppeer_t::send_event_t::connection_t send_event_connection;
    rtppeer_t::status_change_event_t::connection_t
        status_change_event_connection;

    rtppeer_t::midi_event_t::connection_t midi_event_connection;
    poller_t::timer_t timer_connection;

    connection_t<float> timer_ck_connection;
    poller_t::timer_t timer_ck;
    network_address_t addr;
  };
  std::vector<peer_data_t> peers;

  std::string name;
  udppeer_t control;
  udppeer_t midi;
  udppeer_t::on_read_t::connection_t on_read_control;
  udppeer_t::on_read_t::connection_t on_read_midi;

  rtpserver_t(std::string name, const std::string &port);
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ~rtpserver_t();

  int port() { return control.get_address().port(); }

  // Returns the peer for that packet, or nullptr
  std::shared_ptr<rtppeer_t> get_peer_by_packet(io_bytes_reader &b,
                                                rtppeer_t::port_e port);
  std::shared_ptr<rtppeer_t> get_peer_by_initiator_id(uint32_t initiator_id);
  std::shared_ptr<rtppeer_t> get_peer_by_ssrc(uint32_t ssrc);

  void create_peer_from(io_bytes_reader &&buffer, const network_address_t &addr,
                        rtppeer_t::port_e port);

  void send_midi_to_all_peers(const io_bytes_reader &bufer);
  // Call from time to time when there are events to
  // avoid disconnection. Normally on cks.
  void rearm_ck_timeout(int peerdata_id);

  void data_ready(const io_bytes_reader &data, const network_address_t &addr,
                  rtppeer_t::port_e port);
  void sendto(const io_bytes_reader &b, rtppeer_t::port_e port,
              network_address_t &address, int remote_base_port);

  peer_data_t *find_peer_data_by_id(int id);
};
} // namespace rtpmidid

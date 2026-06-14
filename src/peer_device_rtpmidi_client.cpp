/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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

#include "peer_device_rtpmidi_client.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidid/rtppeer.hpp"
#include "utils.hpp"
#include <memory>

namespace rtpmididns {
peer_device_rtpmidi_client_t::peer_device_rtpmidi_client_t(
    std::shared_ptr<rtpmidid::rtpclient_t> peer_)
    : peer(peer_) {

  midi_connection = peer->peer.midi_event.connect(
      [this](const rtpmidid::io_bytes_reader &data) {
        enqueue_to_router(mididata_t{data});
      });

  status_change_event_connection = peer->peer.status_change_event.connect(
      [this](rtpmidid::rtppeer_t::status_e status) {
        DEBUG(
            "Status changed: {}. peer: {}. Add rtpmidi peer and alsa port too.",
            status, peer->peer.remote_name);
        /* Belt-and-braces guard: status_change_event fires on the poller thread
           as soon as the rtpclient's first OK arrives, which can race with
           midirouter_t::add_peer() on a slow router queue or in test setups
           that never wire this peer to a router. Without this check we would
           dereference a null router shared_ptr (observed crash for
           "Peak-Peak MIDI 1" on MIDI_PORT). */
        if (!router) {
          WARNING("component=rtpmidi_client peer_device_rtpmidi_client_t {} got status change {} before "
                  "being attached to a router; ignoring.",
                  peer->peer.remote_name, static_cast<int>(status));
          return;
        }
        if (status == rtpmidid::rtppeer_t::status_e::CONNECTED) {
          router->event(peer_id, midipeer_event_e::CONNECTED_PEER);
        } else if (status >= rtpmidid::rtppeer_t::status_e::DISCONNECTED) {
          router->event(peer_id, midipeer_event_e::DISCONNECTED_PEER);
        }
      });
}

peer_device_rtpmidi_client_t::peer_device_rtpmidi_client_t(const std::string &name,
                                                   const std::string &hostname,
                                                   const std::string &port)
    : peer_device_rtpmidi_client_t(std::make_shared<rtpmidid::rtpclient_t>(name)) {
  /* Do NOT trigger peer->add_server_address() here. The rtpclient's connect()
     would start the state machine on the poller thread, and on a fast/local
     network the first OK can arrive (and fire status_change_event) BEFORE the
     caller manages to install us in a router via add_peer(). The lambda above
     would then dereference an empty `router` shared_ptr. Stash the endpoint
     and drain it from on_router_attached() instead. */
  pending_server_addresses_.push_back({hostname, port});
}

peer_device_rtpmidi_client_t::~peer_device_rtpmidi_client_t() {}

void peer_device_rtpmidi_client_t::on_router_attached() {
  if (pending_server_addresses_.empty())
    return;
  auto endpoints = std::move(pending_server_addresses_);
  pending_server_addresses_.clear();
  peer->add_server_addresses(endpoints);
}

void peer_device_rtpmidi_client_t::send_midi(midipeer_id_t from,
                                         const mididata_t &data) {
  peer->peer.send_midi(data);
};

router_peer_row_t peer_device_rtpmidi_client_t::status() const {
  router_peer_row_t row;
  row.name = peer->peer.remote_name;
  row.peer = rtp_peer_status_from(peer->peer);
  if (!peer->address_port_known.empty()) {
    const auto &ep = peer->address_port_known.front();
    row.connect_hostname = ep.hostname;
    row.connect_port = ep.port;
  }
  return row;
}

} // namespace rtpmididns

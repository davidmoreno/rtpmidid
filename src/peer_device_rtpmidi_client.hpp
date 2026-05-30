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
#pragma once

#include "peer_device.hpp"
#include "dm_json_status.hpp"
#include "rtpmidid/rtpclient.hpp"
#include "rtpmidid/signal.hpp"
#include "rtpmidid/utils.hpp"

namespace rtpmidid {
class io_bytes_reader;
} // namespace rtpmidid

namespace rtpmididns {

/**
 * @short A remote peer connection
 *
 * Just does the basic communication with the given remote peer.
 *
 * Data from network to the peer is managed somewhere else, normally
 * a rtpmidid::rtpclient_t or rtpmidid::rtpserver_t object.
 */
/** One-to-one peer for an outbound RTP-MIDI client connection. */
class peer_device_rtpmidi_client_t : public peer_device_t {
  NON_COPYABLE_NOR_MOVABLE(peer_device_rtpmidi_client_t);

public:
  std::shared_ptr<rtpmidid::rtpclient_t> peer;
  rtpmidid::rtppeer_t::midi_event_t::connection_t midi_connection;
  rtpmidid::rtppeer_t::status_change_event_t::connection_t
      status_change_event_connection;

  peer_device_rtpmidi_client_t(std::shared_ptr<rtpmidid::rtpclient_t> peer);
  peer_device_rtpmidi_client_t(const std::string &name, const std::string &hostname,
                           const std::string &port);
  ~peer_device_rtpmidi_client_t() override;
  void send_midi(midipeer_id_t from, const mididata_t &) override;
  router_peer_row_t status() const override;
  /* Deferred server addresses, populated by the (name,host,port) ctor and
     drained once the peer has been attached to a router. Triggering the
     rtpclient connect() before `router` is set leads to status_change_event
     racing with `add_peer` and dereferencing a null router on the poller
     thread. */
  void on_router_attached() override;

  static std::optional<std::string>
  stable_id_from_row(const router_peer_row_t &row);

protected:
  peer_kind_e peer_kind() const override {
    return peer_kind_e::device_rtpmidi_client;
  }
  std::optional<std::string> compute_stable_id_impl() const override;

private:
  std::vector<rtpmidid::rtpclient_t::endpoint_t> pending_server_addresses_;
};
} // namespace rtpmididns

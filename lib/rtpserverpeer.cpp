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

#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpserver.hpp>
#include <rtpmidid/rtpserverpeer.hpp>

namespace rtpmidid {
rtpserverpeer_t::rtpserverpeer_t(io_bytes_reader &&buffer,
                                 const network_address_t &addr,
                                 rtppeer_t::port_e port,
                                 const std::string &name, rtpserver_t *srv) {
  server = srv;
  id = server->max_peer_data_id++;

  peer = std::make_shared<rtppeer_t>(name);

  DEBUG("Connected from {}", addr.to_string());
  peer->remote_address = addr.dup();
  peer->local_address = srv->control.get_address().dup();

  address = addr.dup();

  // Setup some callbacks
  setup_connections();
  // Finally pass the data to the peer
  peer->data_ready(std::move(buffer), port);

  DEBUG("rtpserverpeer_t::rtpserverpeer_t({});", id);
}

void rtpserverpeer_t::setup_connections() {
  send_event_connection = peer->send_event.connect(
      [this](const io_bytes_reader &buff, rtppeer_t::port_e port) {
        sendto(buff, port);
      });

  status_change_event_connection = peer->status_change_event.connect(
      [this](rtppeer_t::status_e st) { status_change(st); });

  midi_event_connection = peer->midi_event.connect(
      [this](const io_bytes_reader &data) { server->midi_event(data); });

  timer_connection = poller.add_timer_event(std::chrono::seconds(5), [this]() {
    if (peer->status == rtppeer_t::status_e::CONTROL_CONNECTED) {
      DEBUG("Timeout waiting for MIDI connection. Disconnecting.");
      peer->disconnect();
    }
  });

  ck_event_connection =
      peer->ck_event.connect([this](float) { rearm_ck_timeout(); });
}

rtpserverpeer_t::rtpserverpeer_t(rtpserverpeer_t &&other) {
  id = -1;
  *this = std::move(other);
  DEBUG("rtpserverpeer_t::rtpserverpeer_t({}) DUP;", id);
}

rtpserverpeer_t::~rtpserverpeer_t() {
  send_event_connection.disconnect();
  status_change_event_connection.disconnect();
  ck_event_connection.disconnect();
  midi_event_connection.disconnect();
  timer_connection.disable();
  DEBUG("rtpserverpeer_t::~rtpserverpeer_t({})", id);
  id = -2;
}

void rtpserverpeer_t::rearm_ck_timeout() {
  timer_connection.disable();

  // If no signal in 60 secs, remove the peer
  timer_connection = poller.add_timer_event(std::chrono::seconds(60),
                                            [this]() { peer->disconnect(); });
}

rtpserverpeer_t &rtpserverpeer_t::operator=(rtpserverpeer_t &&other) {
  DEBUG("rtpserverpeer_t::operator=({} -> {})", id, other.id);
  this->id = other.id;
  this->address = std::move(other.address);
  this->server = std::move(other.server);
  this->peer = std::move(other.peer);

  other.send_event_connection.disconnect();
  other.status_change_event_connection.disconnect();
  other.ck_event_connection.disconnect();
  other.midi_event_connection.disconnect();
  other.timer_connection.disable();
  other.id = -1;

  setup_connections();

  return *this;
}

void rtpserverpeer_t::sendto(const io_bytes_reader &buff,
                             rtppeer_t::port_e port) {
  server->sendto(buff, port, address, peer->remote_address.port());
}

void rtpserverpeer_t::status_change(rtppeer_t::status_e st) {
  DEBUG("rptserverpeer_t status change to {}", st);

  server->status_change_event(peer, st);
  if (st == rtppeer_t::CONNECTED) {
    rearm_ck_timeout();
  } else if (rtppeer_t::is_disconnected(st)) {
    DEBUG("Remove from server the peer {}, status: {}", id, st);
    server->remove_peer(id);
  }
}
} // namespace rtpmidid
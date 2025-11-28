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

#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/network.hpp>
#include <rtpmidid/networkaddress.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpclient.hpp>
#include <rtpmidid/rtppeer.hpp>
#include <rtpmidid/udppeer.hpp>

using namespace std::chrono_literals;
using namespace rtpmidid;

/**
 * @short Create the rtp client
 *
 * @param name Name of the client.
 *
 * This client will try to connect to the list of `add_server_address`
 * addresses:ports, one by one.
 *
 * There is an internal state machine that helps try all combinations of name
 * resolves to ensure connection. It can even connect control and then fail.
 * (TODO: add timer)
 *
 */
rtpclient_t::rtpclient_t(const std::string name) : peer(std::move(name)) {
  peer.initiator_id = ::rtpmidid::rand_u32();
  send_connection = peer.send_event.connect([this](const io_bytes_reader &data,
                                                   rtppeer_t::port_e port) {
    try {
      this->sendto(data, port);
    } catch (const network_exception &e) {
      ERROR("Error sending data to {}. {}",
            port == rtppeer_t::CONTROL_PORT
                ? control_peer.get_address().to_string()
                : midi_peer.get_address().to_string(),
            e.what());
      peer.status_change_event(rtppeer_t::status_e::DISCONNECTED_NETWORK_ERROR);
    }
  });
}

rtpclient_t::~rtpclient_t() {
  if (peer.is_connected()) {
    try {
      peer.send_goodbye(rtppeer_t::CONTROL_PORT);
      peer.send_goodbye(rtppeer_t::MIDI_PORT);
    } catch (network_exception &e) {
      ERROR("Removing client without sending proper goodbye. If reconnected "
            "may misbehave.");
    }
  }
}

void rtpclient_t::sendto(const io_bytes &pb, rtppeer_t::port_e port) {
  packet_t packet(pb.start, pb.size());

  ssize_t res;
  if (port == rtppeer_t::MIDI_PORT) {
    res = midi_peer.sendto(packet, midi_address);
  } else if (port == rtppeer_t::CONTROL_PORT) {
    res = control_peer.sendto(packet, control_address);
  } else {
    throw exception("Unknown port {}", port);
  }

  if (res == -1) {
    ERROR("Client: Could not send all data to {}. Sent {}. {} ({})", port, res,
          strerror(errno), errno);
    throw network_exception(errno);
  }
}

void rtpclient_t::reset() { peer.reset(); }

void rtpclient_t::add_server_address(const std::string &address,
                                     const std::string &port) {
  address_port_known.push_back({address, port});
  address_port_pending.push_back({address, port});
  connect();
}

void rtpclient_t::add_server_addresses(
    const std::vector<rtpclient_t::endpoint_t> &endpoints) {
  for (auto &endpoint : endpoints) {
    address_port_known.push_back(endpoint);
    address_port_pending.push_back(endpoint);
  }
  connect();
}

void rtpclient_t::connect() {
  if (state == WaitToStart)
    handle_event(Started);
}

///
/// State mahcine implementation
///

void rtpclient_t::state_wait_to_start() {}

void rtpclient_t::state_prepare_next_dns() {
  if (address_port_pending.empty()) {
    handle_event(ConnectListExhausted);
    return;
  }
  resolve_next_dns_endpoint = address_port_pending.back();
  address_port_pending.pop_back();

  handle_event(NextReady);
}

void rtpclient_t::state_resolve_next_ip_port() {
  // First time, get next address and resolve
  if (!resolve_next_dns_sockaddress_list.is_valid()) {
    resolve_next_dns_sockaddress_list = network_address_list_t(
        resolve_next_dns_endpoint.hostname, resolve_next_dns_endpoint.port);
    resolve_next_dns_sockaddress_list_I =
        resolve_next_dns_sockaddress_list.begin();
  } else {
    // Following use the next resolved item
    resolve_next_dns_sockaddress_list_I++;
  }

  // If any left, go for it, if not failed resolve this address port pair
  if (resolve_next_dns_sockaddress_list_I !=
      resolve_next_dns_sockaddress_list.end()) {
    control_address = (*resolve_next_dns_sockaddress_list_I).dup();
    midi_address =
        network_address_list_t(control_address.ip(),
                               std::to_string(control_address.port() + 1))
            .get_first()
            .dup();

    DEBUG("Try to connect to address: {}", control_address.to_string());

    handle_event(Resolved);
  } else {
    resolve_next_dns_sockaddress_list = network_address_list_t();
    handle_event(ConnectListExhausted);
  }
}

void rtpclient_t::state_connect_control() {
  // Any, but could force the initial control port here
  control_peer.open(network_address_list_t("::", local_base_port_str));
  control_on_read_connection = control_peer.on_read.connect(
      [this](const packet_t &packet, const network_address_t &) {
        DEBUG("Data ready for control!");
        io_bytes_reader data(packet.get_data(), packet.get_size());
        this->peer.data_ready(std::move(data), rtppeer_t::CONTROL_PORT);
      });

  if (!control_peer.is_open()) {
    ERROR("Could not connect {}:{} to control port",
          resolve_next_dns_endpoint.hostname, resolve_next_dns_endpoint.port);
    handle_event(ConnectFailed);
    return;
  }
  local_base_port = control_peer.get_address().port();

  control_connected_event_connection =
      peer.status_change_event.connect([this](rtppeer_t::status_e status) {
        control_connected_event_connection.disconnect();
        if (status != rtppeer_t::CONTROL_CONNECTED) {
          handle_event(ConnectFailed);
          return;
        }

        auto address = control_peer.get_address();
        INFO("Connected control port {} to {}:{}", address.port(),
             address.hostname(), address.port());
        handle_event(Connected);
      });

  timer = poller.add_timer_event(connect_timeout, [this] {
    ERROR("Timeout connecting to control port");
    control_connected_event_connection.disconnect();
    handle_event(ConnectFailed);
  });

  peer.connect_to(rtppeer_t::CONTROL_PORT);
}

void rtpclient_t::state_connect_midi() {
  timer.disable();
  midi_peer.open(
      network_address_list_t("::", std::to_string(local_base_port + 1)));

  if (!midi_peer.is_open()) {
    ERROR("Could not connect {}:{} to midi port", midi_address.ip(),
          midi_address.port() + 1);
    handle_event(ConnectFailed);
    return;
  }

  midi_on_read_connection = midi_peer.on_read.connect(
      [this](const packet_t &packet, const network_address_t &) {
        io_bytes_reader data(packet.get_data(), packet.get_size());
        // DEBUG("Data ready for midi!");
        this->peer.data_ready(std::move(data), rtppeer_t::MIDI_PORT);
      });

  midi_connected_event_connection =
      peer.status_change_event.connect([this](rtppeer_t::status_e status) {
        midi_connected_event_connection.disconnect();
        if (status != rtppeer_t::CONNECTED) {
          handle_event(ConnectFailed);
          return;
        }
        auto address = midi_peer.get_address();
        // DEBUG("Connected midi port {} to {}:{}", address.port(),
        //       address.hostname(), address.port());
        handle_event(Connected);
      });
  timer = poller.add_timer_event(5s, [this] {
    ERROR("Timeout connecting to midi port");
    control_connected_event_connection.disconnect();
    handle_event(ConnectFailed);
  });

  // event
  peer.connect_to(rtppeer_t::MIDI_PORT);
}

void rtpclient_t::state_disconnect_control() {
  timer.disable();
  peer.send_goodbye(rtppeer_t::CONTROL_PORT);
  control_peer.close();
  handle_event(ConnectFailed);
}

void rtpclient_t::state_all_connected() {
  INFO("Connected");
  peer.remote_address = control_address.dup();
  peer.local_address = control_peer.get_address().dup();

  timer.disable();
  ck_count = 0;
  handle_event(SendCK);
}

void rtpclient_t::state_send_ck_short() {
  timer = poller.add_timer_event(connect_timeout,
                                 [this] { handle_event(Timeout); });
  ck_connection = peer.ck_event.connect([this](float ms) {
    timer.disable();
    if (ck_count < 6) {
      handle_event(WaitSendCK);
    } else {
      handle_event(LatencyMeasured);
    }
    ck_connection = peer.ck_event.connect([](float ms) {
      WARNING("OUT OF ORDER CK0 received, latency: {} ms", ms);
    });
  });
  ck_count++;
  peer.send_ck0();
}

void rtpclient_t::state_wait_send_ck_short() {
  timer =
      poller.add_timer_event(ck_short_period, [this] { handle_event(SendCK); });
}

void rtpclient_t::state_send_ck_long() {
  ck_count++;
  peer.send_ck0();
  timer = poller.add_timer_event(connect_timeout,
                                 [this] { handle_event(Timeout); });
  ck_connection = peer.ck_event.connect([this](float ms) {
    timer.disable();
    handle_event(WaitSendCK);
    ck_connection = peer.ck_event.connect([](float ms) {
      WARNING("OUT OF ORDER CK0 received, latency: {} ms", ms);
    });
  });
}

void rtpclient_t::state_wait_send_ck_long() {
  ck_connection.disconnect();
  timer =
      poller.add_timer_event(ck_long_period, [this] { handle_event(SendCK); });
}

void rtpclient_t::state_disconnect_because_cktimeout() {
  INFO("Disconnecting because of CK timeout");
  peer.disconnect();
  handle_event(ConnectFailed);
}

void rtpclient_t::state_error() {
  peer.disconnect();
  ERROR("Error at rtpclient_t. Can't connect or disconnected. Will try to "
        "connect again in {}ms",
        reconnect_timeout.count());
  timer = poller.add_timer_event(reconnect_timeout,
                                 [this] { handle_event(Connect); });
}

void rtpclient_t::state_try_connect_to_all_known_dns() {
  address_port_pending = address_port_known;
  handle_event(Connect);
}

#include "rtpclient_statemachine.cpp"

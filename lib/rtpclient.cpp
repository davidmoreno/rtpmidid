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
  send_connection = peer.send_event.connect(
      [this](const io_bytes_reader &data, rtppeer_t::port_e port) {
        try {
          this->sendto(data, port);
        } catch (const network_exception &e) {
          ERROR("Error sending data to {}. {}",
                port == rtppeer_t::CONTROL_PORT
                    ? control_peer.get_address().to_string()
                    : midi_peer.get_address().to_string(),
                e.what());
          peer.disconnect_event(rtppeer_t::disconnect_reason_e::NETWORK_ERROR);
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

/**
 * Send the periodic latency and connection checks
 *
 * At first six times as received confirmation from other end. Then Every 10
 * secs. Check connected() function for actuall recall code.
 *
 * This just checks timeout and sends the ck.
 */
void rtpclient_t::start_ck_timers() {
  connect_timer.disable();

  ck_connection = peer.ck_event.connect([this](float ms) {
    ck_timeout.disable();
    if (timerstate < 6) {
      timer_ck =
          poller.add_timer_event(1500ms, [this] { send_ck0_with_timeout(); });
      timerstate++;
    } else {
      timer_ck =
          poller.add_timer_event(10s, [this] { send_ck0_with_timeout(); });
    }
  });
  send_ck0_with_timeout();
}

void rtpclient_t::send_ck0_with_timeout() {
  peer.send_ck0();
  ck_timeout = poller.add_timer_event(5s, [this] {
    peer.disconnect_event(rtppeer_t::disconnect_reason_e::CK_TIMEOUT);
  });
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

// Make the order follow as written at statemachines.md, so can
// filled with simple copy/paste and multicursors
struct state_change_t {
  rtpclient_t::states_e state;
  rtpclient_t::states_e next_state;
  rtpclient_t::event_e event;
};

state_change_t state_changes[] = {
    {rtpclient_t::WaitToStart, rtpclient_t::PrepareNextDNS,
     rtpclient_t::Started},
    {rtpclient_t::PrepareNextDNS, rtpclient_t::ResolveNextIpPort,
     rtpclient_t::NextReady},
    {rtpclient_t::PrepareNextDNS, rtpclient_t::ErrorCantConnect,
     rtpclient_t::ResolveListExhausted},
    {rtpclient_t::ResolveNextIpPort, rtpclient_t::PrepareNextDNS,
     rtpclient_t::ConnectListExhausted},
    {rtpclient_t::ResolveNextIpPort, rtpclient_t::ResolveNextIpPort,
     rtpclient_t::ResolveFailed},
    {rtpclient_t::ResolveNextIpPort, rtpclient_t::ConnectControl,
     rtpclient_t::Resolved},
    {rtpclient_t::ConnectControl, rtpclient_t::ResolveNextIpPort,
     rtpclient_t::ConnectFailed},
    {rtpclient_t::ConnectControl, rtpclient_t::ConnectMidi,
     rtpclient_t::Connected},
    {rtpclient_t::ConnectMidi, rtpclient_t::AllConnected,
     rtpclient_t::Connected},
    {rtpclient_t::ConnectMidi, rtpclient_t::DisconnectControl,
     rtpclient_t::ConnectFailed},
    {rtpclient_t::DisconnectControl, rtpclient_t::ResolveNextIpPort,
     rtpclient_t::ConnectFailed},
};

void rtpclient_t::state_machine(rtpclient_t::event_e event) {
  // The state machine itself
  rtpclient_t::states_e next_state =
      ErrorCantConnect; // default state, if not changed
  for (auto &change : state_changes) {
    if (state == change.state && event == change.event) {
      next_state = change.next_state;
      break;
    }
  }
  INFO("State machine: {} -[{}]-> {}", state, event, next_state);

  state = next_state;

  // Call the new state functions
  switch (state) {
  case WaitToStart:
    break;
  case PrepareNextDNS:
    resolve_next_dns();
    break;
  case ResolveNextIpPort:
    connect_next_ip_port();
    break;
  case ConnectControl:
    connect_control();
    break;
  case ConnectMidi:
    connect_midi();
    break;
  case AllConnected:
    all_connected();
    break;
  case ErrorCantConnect:
    error_cant_connect();
    break;
  case DisconnectControl:
    disconnect_control();
    break;
  }
}

void rtpclient_t::resolve_next_dns() {
  if (address_port_pending.empty()) {
    state_machine(ConnectListExhausted);
    return;
  }
  resolve_next_dns_endpoint = address_port_pending.back();
  address_port_pending.pop_back();

  state_machine(NextReady);
}

void rtpclient_t::connect_next_ip_port() {
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

    state_machine(Resolved);
  } else {
    resolve_next_dns_sockaddress_list = network_address_list_t();
    state_machine(ConnectListExhausted);
  }
}

void rtpclient_t::connect_control() {
  // Any, but could force the initial control port here
  control_peer.open(network_address_list_t("::", "0"));
  control_on_read_connection = control_peer.on_read.connect(
      [this](const packet_t &packet, const network_address_t &) {
        DEBUG("Data ready for control!");
        io_bytes_reader data(packet.get_data(), packet.get_size());
        this->peer.data_ready(std::move(data), rtppeer_t::CONTROL_PORT);
      });

  if (!control_peer.is_open()) {
    ERROR("Could not connect {}:{} to control port",
          resolve_next_dns_endpoint.hostname, resolve_next_dns_endpoint.port);
    state_machine(ConnectFailed);
    return;
  }
  local_base_port = control_peer.get_address().port();

  control_connected_event_connection = peer.connected_event.connect(
      [this](const std::string &name, rtppeer_t::status_e status) {
        control_connected_event_connection.disconnect();
        if (status != rtppeer_t::CONTROL_CONNECTED) {
          state_machine(ConnectFailed);
          return;
        }

        auto address = control_peer.get_address();
        INFO("Connected control port {} to {}:{}", address.port(),
             address.hostname(), address.port());
        state_machine(Connected);
      });
  peer.connect_to(rtppeer_t::CONTROL_PORT);
}

void rtpclient_t::connect_midi() {
  midi_peer.open(
      network_address_list_t("::", std::to_string(local_base_port + 1)));

  if (!midi_peer.is_open()) {
    ERROR("Could not connect {}:{} to midi port", midi_address.ip(),
          midi_address.port() + 1);
    state_machine(ConnectFailed);
    return;
  }

  midi_on_read_connection = midi_peer.on_read.connect(
      [this](const packet_t &packet, const network_address_t &) {
        io_bytes_reader data(packet.get_data(), packet.get_size());
        // DEBUG("Data ready for midi!");
        this->peer.data_ready(std::move(data), rtppeer_t::MIDI_PORT);
      });

  midi_connected_event_connection = peer.connected_event.connect(
      [this](const std::string &name, rtppeer_t::status_e status) {
        midi_connected_event_connection.disconnect();
        if (status != rtppeer_t::CONNECTED) {
          state_machine(ConnectFailed);
          return;
        }
        auto address = midi_peer.get_address();
        // DEBUG("Connected midi port {} to {}:{}", address.port(),
        //       address.hostname(), address.port());
        state_machine(Connected);
      });
  // event
  peer.connect_to(rtppeer_t::MIDI_PORT);
}

void rtpclient_t::disconnect_control() {
  // TODO send goodbye to control
  control_peer.close();
  state_machine(ConnectFailed);
}

void rtpclient_t::all_connected() { INFO("Connected"); }
void rtpclient_t::error_cant_connect() {
  connected_event("", rtppeer_t::NOT_CONNECTED);
}

void rtpclient_t::add_server_address(const std::string &address,
                                     const std::string &port) {
  address_port_pending.push_back({address, port});
}

void rtpclient_t::connect() { state_machine(Started); }

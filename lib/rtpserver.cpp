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

#include <algorithm>
#include <sys/types.h>
#include <unistd.h>

#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/network.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtppeer.hpp>
#include <rtpmidid/rtpserver.hpp>

using namespace rtpmidid;

// NOLINTNEXTLINE(bugprone-swappable-parameters)
rtpserver_t::rtpserver_t(std::string _name, const std::string &port)
    : name(std::move(_name)) {

  control.open("::", port);
  midi.open("::", control.get_address().port() + 1);

  on_read_control = control.on_read.connect(
      [&](const packet_t &packet, const network_address_t &from) {
        io_bytes_reader iobytes(packet.get_data(), packet.get_size());
        data_ready(iobytes, from, rtppeer_t::CONTROL_PORT);
      });
  on_read_midi = midi.on_read.connect(
      [&](const packet_t &packet, const network_address_t &from) {
        io_bytes_reader iobytes(packet.get_data(), packet.get_size());
        data_ready(iobytes, from, rtppeer_t::MIDI_PORT);
      });

  INFO("Listening RTP MIDI connections at {} / {}, with name: '{}'",
       control.get_address().to_string(), midi.get_address().to_string(), name);
}

// NOLINTNEXTLINE(bugprone-exception-escape)
rtpserver_t::~rtpserver_t() {
  DEBUG("~rtpserver_t({})", name);

  // Must clear here, to be able to use the control and midi
  // sockets
  // This calls indirectly all the disconnects, that will remove from the
  // array, and create a segfault. So we create a shared_ptr copy and use that.
  // I did try to be smarter, but the peers list can be modified when sending
  // goodbyes, so better copy peers to be safe.
  std::vector<std::shared_ptr<rtppeer_t>> peers_copy(peers.size());
  for (auto &peerinfo : peers) {
    DEBUG("Peer still in the server list: {}", peerinfo.id);
    peers_copy.push_back(peerinfo.peer);
  }

  // We have proper copyes, so we can proceed with the send goodbyes.. and then
  // maybe delete the peers.
  for (auto &peer : peers_copy) {
    if (peer) {
      if (peer->status & rtppeer_t::CONTROL_CONNECTED)
        peer->send_goodbye(rtppeer_t::CONTROL_PORT);
      if (peer->status & rtppeer_t::MIDI_CONNECTED)
        peer->send_goodbye(rtppeer_t::MIDI_PORT);
    }
  }
}

rtpserverpeer_t *rtpserver_t::find_peer_by_packet(io_bytes_reader &buffer,
                                                  rtppeer_t::port_e port) {
  // Commands may be by SSRC or initiator_id
  auto command =
      rtppeer_t::commands_e((uint16_t(buffer.start[2]) << 8) + buffer.start[3]);

  switch (command) {
  case rtppeer_t::IN:
  case rtppeer_t::OK:
  case rtppeer_t::NO: {
    buffer.position = buffer.start + 8;
    auto initiator_id = buffer.read_uint32();
    buffer.position = buffer.start;

    return find_peer_by_initiator_id(initiator_id);
  }
  case rtppeer_t::BY: {
    buffer.position = buffer.start + 12;
    auto ssrc_id = buffer.read_uint32();
    buffer.position = buffer.start;

    return find_peer_by_ssrc(ssrc_id);
  }
  case rtppeer_t::CK:
  case rtppeer_t::RS: {
    buffer.position = buffer.start + 4;
    auto ssrc = buffer.read_uint32();
    buffer.position = buffer.start;
    return find_peer_by_ssrc(ssrc);
  }
  default:
    if (port == rtppeer_t::MIDI_PORT && (buffer.start[1] & 0x7F) == 0x61) {
      buffer.read_uint32();
      buffer.read_uint32();
      auto ssrc = buffer.read_uint32();
      buffer.position = buffer.start;

      return find_peer_by_ssrc(ssrc);
    }
    DEBUG("Unknown COMMAND id {:X} / {:X}", int(command), buffer.start[1]);
    return nullptr;
  }
}

rtpserverpeer_t *rtpserver_t::find_peer_by_ssrc(uint32_t ssrc) {
  for (auto &peerdata : peers) {
    if (peerdata.peer->remote_ssrc == ssrc) {
      return &peerdata;
    }
  }
  return nullptr;
}

rtpserverpeer_t *rtpserver_t::find_peer_by_initiator_id(uint32_t initiator_id) {
  for (auto &peerdata : peers) {
    if (peerdata.peer->initiator_id == initiator_id) {
      return &peerdata;
    }
  }
  return nullptr;
}

void rtpserver_t::data_ready(const io_bytes_reader &data,
                             const network_address_t &addr,
                             rtppeer_t::port_e port) {
  io_bytes_reader buffer(data);
  auto peer = find_peer_by_packet(buffer, port);
  if (peer) {
    peer->peer->data_ready(std::move(buffer), port);
  } else {
    if (rtppeer_t::is_command(buffer) && buffer.start[2] == 'I' &&
        buffer.start[3] == 'N') {
      create_peer_from(std::move(buffer), addr, port);
    } else
      DEBUG("Unknown peer {}, and not connect on control. Ignoring {} port.",
            addr.to_string(), port);

    buffer.print_hex(true);
  }
}

void rtpserver_t::sendto(const io_bytes_reader &pb, rtppeer_t::port_e port,
                         network_address_t &address, int remote_base_port) {
  if (port == rtppeer_t::MIDI_PORT) {
    address.set_port(remote_base_port + 1);
    midi.sendto(packet_t(pb.start, pb.size()), address);
  } else {
    address.set_port(remote_base_port);
    control.sendto(packet_t(pb.start, pb.size()), address);
  }
}

void rtpserver_t::create_peer_from(io_bytes_reader &&buffer,
                                   const network_address_t &addr,
                                   rtppeer_t::port_e port) {
  peers.emplace_back(std::move(buffer), addr, port, name, this);
}

rtpserverpeer_t *rtpserver_t::find_peer_data_by_id(int id) {
  auto peerdata = std::find_if(
      peers.begin(), peers.end(),
      [id](const rtpserverpeer_t &datapeer) { return datapeer.id == id; });
  if (peerdata == peers.end())
    return nullptr;
  return &(*peerdata);
}

void rtpserver_t::send_midi_to_all_peers(const io_bytes_reader &buffer) {
  for (auto &speers : peers) {
    speers.peer->send_midi(buffer);
  }
}

void rtpserver_t::remove_peer(int peer_id) {
  auto peerdata = find_peer_data_by_id(peer_id);
  if (!peerdata)
    return;

  peers.erase(std::remove_if(peers.begin(), peers.end(),
                             [peer_id](const rtpserverpeer_t &datapeer) {
                               return datapeer.id == peer_id;
                             }));
}
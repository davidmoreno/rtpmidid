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

#include "midirouter.hpp"
#include "json.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "rtpmidid/logger.hpp"

namespace rtpmididns {

midirouter_t::midirouter_t() : max_id(1) {}
midirouter_t::~midirouter_t() {}

uint32_t midirouter_t::add_peer(std::shared_ptr<midipeer_t> peer) {
  auto peer_id = max_id++;
  peer->peer_id = peer_id;
  try {
    peer->router = shared_from_this();
  } catch (const std::exception &exc) {
    ERROR("Error on SHARED FROM THIS! Make sure that the router is a "
          "std::shared_ptr<midirouter_t>. {} {}",
          (void *)this, exc.what());
    throw;
  }

  peers[peer_id] = peerconnection_t{
      peer_id,
      peer,
      {},
  };
  INFO("Added peer {}", peer_id);

  return peer_id;
}

std::shared_ptr<midipeer_t> midirouter_t::get_peer_by_id(peer_id_t peer_id) {
  auto peer = peers.find(peer_id);
  if (peer != peers.end()) {
    return peer->second.peer;
  }
  return nullptr;
}

peerconnection_t *midirouter_t::get_peerdata_by_id(peer_id_t peer_id) {
  auto peer = peers.find(peer_id);
  if (peer != peers.end()) {
    return &peer->second;
  }
  return nullptr;
}

void midirouter_t::peer_connection_loop(
    peer_id_t peer_id, std::function<void(std::shared_ptr<midipeer_t>)> func) {
  auto peerdata = get_peerdata_by_id(peer_id);
  if (!peerdata) {
    WARNING("Unkown peer {}!", peer_id);
    return;
  }
  for (auto to : peerdata->send_to) {
    // DEBUG("Send data {} to {}", from, to);
    auto peer = get_peer_by_id(to);
    if (peer)
      func(peer);
  }
}

void midirouter_t::remove_peer(peer_id_t peer_id) {
  auto removed = peers.erase(peer_id);
  for (auto &peer : peers) {
    auto &send_to = peer.second.send_to;
    auto I = std::find(send_to.begin(), send_to.end(), peer_id);
    if (I != send_to.end())
      send_to.erase(I);
  }
  if (removed)
    INFO("Removed peer {}", peer_id);
}

void midirouter_t::send_midi(uint32_t from, const mididata_t &data) {
  auto peerdata = get_peerdata_by_id(from);
  if (!peerdata) {
    WARNING("Sending from an unkown peer {}!", from);
    return;
  }

  // DEBUG("Send data to {} peers", peer->second.send_to.size());
  for (auto to : peerdata->send_to) {
    // DEBUG("Send data {} to {}", from, to);
    send_midi(from, to, data);
  }
}

void midirouter_t::send_midi(peer_id_t from, peer_id_t to,
                             const mididata_t &data) {
  auto send_peer = get_peer_by_id(from);
  auto recv_peer = get_peer_by_id(to);
  if (!send_peer || !recv_peer) {
    WARNING("Sending to unkown peer {} -> {}", from, to);
    return;
  }
  send_peer->packets_sent++;
  recv_peer->packets_recv++;
  recv_peer->send_midi(from, data);
}

void midirouter_t::connect(peer_id_t from, peer_id_t to) {
  auto send_peer = get_peerdata_by_id(from);
  auto recv_peer = get_peerdata_by_id(to);
  if (!send_peer || !recv_peer) {
    WARNING("Sending to unkown peer {} -> {}", from, to);
    return;
  }

  send_peer->send_to.push_back(to);
}

json_t midirouter_t::status() {
  std::vector<json_t> routerdata;
  for (auto peer : peers) {
    try {
      auto status = peer.second.peer->status();
      status["id"] = peer.first;
      status["send_to"] = peer.second.send_to;
      status["stats"] = {
          //
          {"recv", peer.second.peer->packets_recv},
          {"sent", peer.second.peer->packets_sent} //
      };

      routerdata.push_back(status);
    } catch (const std::exception &exc) {
      routerdata.push_back(json_t{{"error", exc.what()}});
    }
  }
  return routerdata;
}
} // namespace rtpmididns
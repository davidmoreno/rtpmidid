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

midirouter_t::midirouter_t() {}
midirouter_t::~midirouter_t() {}

uint32_t midirouter_t::add_peer(std::shared_ptr<midipeer_t> peer) {
  if (peer->peer_id) {
    WARNING("Peer already present!");
    return peer->peer_id;
  }

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
  INFO("Added peer type={} peer_id={}", peer->get_type(), peer_id);

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
    WARNING("unknown peer {}!", peer_id);
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
  INFO("Remove peer_id={}", peer_id);
  auto toremove = get_peer_by_id(peer_id);

  // Find all the peers that are connected to this peer and disconnect them
  for (auto &peer : peers) {
    // need to copy the send_to vector to avoid iterator invalidation
    auto send_to_copy = peer.second.send_to;
    for (auto send_to_id : send_to_copy) {
      if (send_to_id == peer_id) {
        disconnect(peer.first, peer_id);
      }
      disconnect(peer_id, peer.first);
    }
  }

  auto removed = peers.erase(peer_id);
  if (removed)
    INFO("Removed peer {}", peer_id);
}

void midirouter_t::send_midi(uint32_t from, const mididata_t &data) {
  auto peerdata = get_peerdata_by_id(from);
  if (!peerdata) {
    WARNING("Sending from an unknown peer {}!", from);
    return;
  }

  peerdata->peer->packets_sent++;
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
    WARNING("Sending to unknown peer {} -> {}", from, to);
    return;
  }
  recv_peer->packets_recv++;
  recv_peer->send_midi(from, data);
}

void midirouter_t::connect(peer_id_t from, peer_id_t to) {
  auto from_peer = get_peerdata_by_id(from);
  auto to_peer = get_peerdata_by_id(to);
  if (!from_peer || !to_peer) {
    WARNING("Sending to unknown peer {} -> {}", from, to);
    return;
  }

  from_peer->send_to.push_back(to);

  from_peer->peer->event(midipeer_event_e::CONNECTED_ROUTER, to);
  to_peer->peer->event(midipeer_event_e::CONNECTED_ROUTER, from);

  INFO("Connect {} -> {}", from, to);
}

void midirouter_t::disconnect(peer_id_t from, peer_id_t to) {
  auto from_peer = get_peerdata_by_id(from);
  auto to_peer = get_peerdata_by_id(to);
  if (!from_peer || !to_peer) {
    WARNING("Sending to unknown peer {} -> {}", from, to);
    return;
  }

  for (auto send_to_id : from_peer->send_to) {
    if (send_to_id == to) {
      from_peer->send_to.erase(
          std::remove(from_peer->send_to.begin(), from_peer->send_to.end(), to),
          from_peer->send_to.end());
      from_peer->peer->event(midipeer_event_e::DISCONNECTED_ROUTER, to);
      to_peer->peer->event(midipeer_event_e::DISCONNECTED_ROUTER, from);
    }
  }

  INFO("Disconnect {} -> {}", from, to);
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
      status["type"] = peer.second.peer->get_type();

      routerdata.push_back(status);
    } catch (const std::exception &exc) {
      routerdata.push_back(json_t{{"error", exc.what()}});
    }
  }
  return routerdata;
}

void midirouter_t::event(peer_id_t from, peer_id_t to, midipeer_event_e event) {
  auto peer = get_peer_by_id(to);
  if (!peer)
    return;
  peer->event(event, from);
}

void midirouter_t::event(peer_id_t from, midipeer_event_e event) {
  auto peerdata = get_peerdata_by_id(from);
  if (!peerdata)
    return;
  for (auto &to_id : peerdata->send_to) {
    auto topeer = get_peer_by_id(to_id);
    if (!topeer)
      continue;
    topeer->event(event, from);
  }
}

void midirouter_t::clear() {
  for (auto &peer : peers) {
    peer.second.peer->router = nullptr;
  }
  peers.clear();
}

} // namespace rtpmididns

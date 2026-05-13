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
#include "mididata.hpp"
#include "midipeer.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/threading_types.hpp"
#include <chrono>
#include <set>

namespace rtpmididns {

midirouter_t::midirouter_t() {}
midirouter_t::~midirouter_t() {
  stop_router_thread();
}

uint32_t midirouter_t::add_peer(std::shared_ptr<midipeer_t> peer) {
  if (peer->peer_id) {
    WARNING("Peer already present!");
    return peer->peer_id;
  }

  std::unique_lock<std::shared_mutex> lock(peers_mutex);
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
  
  // If router thread is running, start this peer's thread too
  // (peers added after setup_threading() need their threads started)
  // Note: We can't check peer->thread_running directly as it's protected,
  // but start_thread() will check it internally
  if (router_running.load()) {
    DEBUG("[MIDI_FLOW] Router: Starting thread for newly added peer {} (router_running={})", 
          peer_id, router_running.load());
    peer->start_thread();
  } else {
    DEBUG("[MIDI_FLOW] Router: Not starting thread for peer {} (router_running={}, will be started in setup_threading)", 
          peer_id, router_running.load());
  }

  return peer_id;
}

std::shared_ptr<midipeer_t> midirouter_t::get_peer_by_id(peer_id_t peer_id) {
  std::shared_lock<std::shared_mutex> lock(peers_mutex);
  auto peer = peers.find(peer_id);
  if (peer != peers.end()) {
    return peer->second.peer;
  }
  return nullptr;
}

peerconnection_t *midirouter_t::get_peerdata_by_id(peer_id_t peer_id) {
  std::shared_lock<std::shared_mutex> lock(peers_mutex);
  auto peer = peers.find(peer_id);
  if (peer != peers.end()) {
    return &peer->second;
  }
  return nullptr;
}

size_t midirouter_t::peer_count() const {
  std::shared_lock<std::shared_mutex> lock(peers_mutex);
  return peers.size();
}

std::vector<peer_id_t> midirouter_t::send_targets_for(peer_id_t from) const {
  std::shared_lock<std::shared_mutex> lock(peers_mutex);
  auto it = peers.find(from);
  if (it == peers.end()) {
    return {};
  }
  return it->second.send_to;
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
  
  // Check if we're already removing this peer (avoid recursive removal)
  {
    std::lock_guard<std::mutex> lock(removing_peers_mutex);
    if (removing_peers.find(peer_id) != removing_peers.end()) {
      WARNING("Already removing peer {}, skipping recursive removal", peer_id);
      return;
    }
    removing_peers.insert(peer_id);
  }
  
  std::unique_lock<std::shared_mutex> lock(peers_mutex);
  auto toremove = peers.find(peer_id);
  if (toremove == peers.end()) {
    std::lock_guard<std::mutex> lock2(removing_peers_mutex);
    removing_peers.erase(peer_id);
    return;
  }

  // Stop the peer's thread before removing (must be done while holding lock to prevent race)
  auto peer_ptr = toremove->second.peer;
  lock.unlock(); // Release lock before stopping thread (thread might need to access router)
  
  // Stop peer thread (this is safe because we still have the shared_ptr)
  try {
    peer_ptr->stop_thread();
  } catch (const std::exception &e) {
    ERROR("Exception stopping peer thread: {}", e.what());
  }
  
  lock.lock(); // Re-acquire lock
  // Re-check peer still exists (might have been removed by another thread)
  toremove = peers.find(peer_id);
  if (toremove == peers.end()) {
    std::lock_guard<std::mutex> lock2(removing_peers_mutex);
    removing_peers.erase(peer_id);
    return;
  }

  // Tear down all edges involving peer_id (fires DISCONNECTED_ROUTER like disconnect())
  {
    const auto outgoing = toremove->second.send_to;
    for (auto to_id : outgoing) {
      disconnect(peer_id, to_id);
    }
  }
  std::vector<peer_id_t> inbound_from;
  for (const auto &p : peers) {
    if (p.first == peer_id) {
      continue;
    }
    for (auto to : p.second.send_to) {
      if (to == peer_id) {
        inbound_from.push_back(p.first);
        break;
      }
    }
  }
  for (auto from_id : inbound_from) {
    disconnect(from_id, peer_id);
  }

  toremove = peers.find(peer_id);
  if (toremove == peers.end()) {
    std::lock_guard<std::mutex> lock2(removing_peers_mutex);
    removing_peers.erase(peer_id);
    return;
  }

  // Clear router reference before erasing
  toremove->second.peer->router = nullptr;
  
  auto removed = peers.erase(peer_id);
  if (removed)
    INFO("Removed peer {}", peer_id);
  
  {
    std::lock_guard<std::mutex> lock2(removing_peers_mutex);
    removing_peers.erase(peer_id);
  }
}

void midirouter_t::send_midi(uint32_t from, const mididata_t &data) {
  std::vector<peer_id_t> send_to_copy;
  {
    std::shared_lock<std::shared_mutex> lock(peers_mutex);
    auto peer_it = peers.find(from);
    if (peer_it == peers.end()) {
      WARNING("Sending from an unknown peer {}!", from);
      return;
    }
    peer_it->second.peer->packets_sent++;
    send_to_copy = peer_it->second.send_to;
  }
  
  // DEBUG("Send data to {} peers", send_to_copy.size());
  for (auto to : send_to_copy) {
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
  // Note: This method assumes caller already holds peers_mutex
  auto from_it = peers.find(from);
  auto to_it = peers.find(to);
  if (from_it == peers.end() || to_it == peers.end()) {
    WARNING("Sending to unknown peer {} -> {}", from, to);
    return;
  }

  auto &from_peer = from_it->second;
  auto &to_peer = to_it->second;
  
  // Check if already connected
  for (auto existing : from_peer.send_to) {
    if (existing == to) {
      return; // Already connected
    }
  }

  from_peer.send_to.push_back(to);

  from_peer.peer->event(midipeer_event_e::CONNECTED_ROUTER, to);
  to_peer.peer->event(midipeer_event_e::CONNECTED_ROUTER, from);

  INFO("Connect {} -> {}", from, to);
}

void midirouter_t::disconnect(peer_id_t from, peer_id_t to) {
  // Note: This method assumes caller already holds peers_mutex
  auto from_it = peers.find(from);
  auto to_it = peers.find(to);
  if (from_it == peers.end() || to_it == peers.end()) {
    WARNING("Sending to unknown peer {} -> {}", from, to);
    return;
  }

  auto &from_peer = from_it->second;
  auto &to_peer = to_it->second;

  for (auto it = from_peer.send_to.begin(); it != from_peer.send_to.end(); ++it) {
    if (*it == to) {
      from_peer.send_to.erase(it);
      from_peer.peer->event(midipeer_event_e::DISCONNECTED_ROUTER, to);
      to_peer.peer->event(midipeer_event_e::DISCONNECTED_ROUTER, from);
      INFO("Disconnect {} -> {}", from, to);
      return;
    }
  }
}

std::vector<router_peer_row_t> midirouter_t::status_rows() const {
  std::shared_lock<std::shared_mutex> lock(peers_mutex);
  std::vector<router_peer_row_t> routerdata;
  for (auto peer : peers) {
    try {
      auto row = peer.second.peer->status();
      row.id = peer.first;
      row.send_to = peer.second.send_to;
      row.type = peer.second.peer->get_type();
      peer_stats_t st;
      st.recv = static_cast<uint64_t>(peer.second.peer->packets_recv.load());
      st.sent = static_cast<uint64_t>(peer.second.peer->packets_sent.load());
      row.stats = st;
      row.internal_latency_ms = peer.second.peer->internal_latency_stats();
      routerdata.push_back(std::move(row));
    } catch (const std::exception &exc) {
      router_peer_row_t row{};
      row.error = exc.what();
      routerdata.push_back(std::move(row));
    }
  }
  return routerdata;
}

void midirouter_t::event(peer_id_t from, peer_id_t to, midipeer_event_e event) {
  std::shared_lock<std::shared_mutex> lock(peers_mutex);
  auto peer_it = peers.find(to);
  if (peer_it == peers.end())
    return;
  peer_it->second.peer->event(event, from);
}

void midirouter_t::event(peer_id_t from, midipeer_event_e event) {
  std::shared_lock<std::shared_mutex> lock(peers_mutex);
  auto peer_it = peers.find(from);
  if (peer_it == peers.end())
    return;
  for (auto &to_id : peer_it->second.send_to) {
    auto topeer_it = peers.find(to_id);
    if (topeer_it == peers.end())
      continue;
    topeer_it->second.peer->event(event, from);
  }
}

void midirouter_t::clear() {
  std::unique_lock<std::shared_mutex> lock(peers_mutex);
  for (auto &peer : peers) {
    peer.second.peer->router = nullptr;
  }
  peers.clear();
}

void midirouter_t::start_router_thread() {
  if (router_running.load()) {
    return;
  }
  router_running = true;
  router_thread = std::thread(&midirouter_t::router_thread_loop, this);
}

void midirouter_t::stop_router_thread() {
  if (!router_running.load()) {
    return;
  }
  router_running = false;
  router_wakeup.notify_one();
  if (router_thread.joinable()) {
    router_thread.join();
  }
}

void midirouter_t::router_thread_loop() {
  using namespace std::chrono_literals;
  
  DEBUG("[MIDI_FLOW] Router: Router thread started");
  while (router_running.load()) {
    rtpmidid::routing_request_t request;
    bool processed = false;

    // Process all available requests
    while (routing_queue.dequeue(request)) {
      processed = true;
      
      switch (request.command) {
      case rtpmidid::routing_command_e::SEND_MIDI: {
        std::shared_lock<std::shared_mutex> lock(peers_mutex);
        auto peer_it = peers.find(request.from_peer_id);
        if (peer_it == peers.end()) {
          WARNING("[MIDI_FLOW] Router: Sending from unknown peer {}!", request.from_peer_id);
          break;
        }
        
        auto &peerdata = peer_it->second;
        peerdata.peer->packets_sent++;
        
        DEBUG("[MIDI_FLOW] Router: Processing SEND_MIDI from peer {}, size={} bytes, to_peer_id={}, num_destinations={}",
              request.from_peer_id, request.data.size(), request.to_peer_id, 
              request.to_peer_id == 0 ? peerdata.send_to.size() : 1);
        
        if (request.to_peer_id == 0) {
          // Broadcast to all connected peers
          DEBUG("[MIDI_FLOW] Router: Broadcasting to {} connected peers", peerdata.send_to.size());
          for (auto to : peerdata.send_to) {
            if (peer_enqueue_fn) {
              // Create packet with move semantics to avoid copying vector
              rtpmidid::midi_packet_t packet(request.from_peer_id, 
                                            request.data.data(), 
                                            request.data.size());
              DEBUG("[MIDI_FLOW] Router: Sending MIDI packet to peer {} (broadcast), size={} bytes",
                    to, packet.data.size());
              // Pass by const ref - the enqueue function will handle the copy/move
              peer_enqueue_fn(to, packet);
            } else {
              WARNING("[MIDI_FLOW] Router: peer_enqueue_fn not set, cannot send to peer {}", to);
            }
          }
        } else {
          // Send to specific peer
          if (peer_enqueue_fn) {
            rtpmidid::midi_packet_t packet(request.from_peer_id,
                                          request.data.data(),
                                          request.data.size());
            DEBUG("[MIDI_FLOW] Router: Sending MIDI packet to peer {} (direct), size={} bytes",
                  request.to_peer_id, packet.data.size());
            peer_enqueue_fn(request.to_peer_id, packet);
          } else {
            WARNING("[MIDI_FLOW] Router: peer_enqueue_fn not set, cannot send to peer {}", request.to_peer_id);
          }
        }
        break;
      }
      
      case rtpmidid::routing_command_e::CONNECT: {
        std::unique_lock<std::shared_mutex> lock(peers_mutex);
        connect(request.from_peer_id, request.to_peer_id);
        break;
      }
      
      case rtpmidid::routing_command_e::DISCONNECT: {
        std::unique_lock<std::shared_mutex> lock(peers_mutex);
        disconnect(request.from_peer_id, request.to_peer_id);
        break;
      }
      
      case rtpmidid::routing_command_e::REMOVE_PEER: {
        // Don't hold lock - remove_peer() will acquire it itself
        // Releasing lock here prevents deadlock
        remove_peer(request.from_peer_id);
        break;
      }
      
      case rtpmidid::routing_command_e::PEER_EVENT: {
        std::shared_lock<std::shared_mutex> lock(peers_mutex);
        if (request.to_peer_id == 0) {
          event(request.from_peer_id, 
                static_cast<midipeer_event_e>(request.data[0]));
        } else {
          event(request.from_peer_id, request.to_peer_id,
                static_cast<midipeer_event_e>(request.data[0]));
        }
        break;
      }
      
      default:
        WARNING("Unknown routing command: {}", 
                static_cast<int>(request.command));
        break;
      }
    }
    
    // Sleep if no work, but wake up periodically to check
    if (!processed) {
      std::unique_lock<std::mutex> lock(router_mutex);
      router_wakeup.wait_for(lock, 10ms, [this] {
        return !router_running.load() || !routing_queue.empty();
      });
    }
  }
}

bool midirouter_t::enqueue_send_midi(peer_id_t from, const mididata_t &data) {
  return enqueue_send_midi(from, 0, data);
}

bool midirouter_t::enqueue_send_midi(peer_id_t from, peer_id_t to,
                                     const mididata_t &data) {
  // If router thread is not running, process synchronously (for tests)
  if (!router_running.load()) {
    if (to == 0) {
      send_midi(from, data);
    } else {
      send_midi(from, to, data);
    }
    return true;
  }

  rtpmidid::routing_request_t request;
  request.command = rtpmidid::routing_command_e::SEND_MIDI;
  request.from_peer_id = from;
  request.to_peer_id = to;
  request.data.assign(data.position, data.position + data.remaining());

  const bool enqueued = routing_queue.enqueue(std::move(request));
  if (!enqueued) {
    WARNING("[MIDI_FLOW] Router: Routing queue full, dropping MIDI packet from "
            "peer {} (to_peer_id={})",
            from, to);
    return false;
  }
  router_wakeup.notify_one();
  return true;
}

bool midirouter_t::enqueue_connect(peer_id_t from, peer_id_t to) {
  // If router thread is not running, process synchronously (for tests)
  if (!router_running.load()) {
    std::unique_lock<std::shared_mutex> lock(peers_mutex);
    connect(from, to);
    return true;
  }
  
  rtpmidid::routing_request_t request;
  request.command = rtpmidid::routing_command_e::CONNECT;
  request.from_peer_id = from;
  request.to_peer_id = to;

  const bool enqueued = routing_queue.enqueue(std::move(request));
  if (!enqueued) {
    WARNING("Routing queue full, dropping connect request");
    return false;
  }
  router_wakeup.notify_one();
  return true;
}

bool midirouter_t::enqueue_disconnect(peer_id_t from, peer_id_t to) {
  // If router thread is not running, process synchronously (for tests)
  if (!router_running.load()) {
    std::unique_lock<std::shared_mutex> lock(peers_mutex);
    disconnect(from, to);
    return true;
  }
  
  rtpmidid::routing_request_t request;
  request.command = rtpmidid::routing_command_e::DISCONNECT;
  request.from_peer_id = from;
  request.to_peer_id = to;

  const bool enqueued = routing_queue.enqueue(std::move(request));
  if (!enqueued) {
    WARNING("Routing queue full, dropping disconnect request");
    return false;
  }
  router_wakeup.notify_one();
  return true;
}

bool midirouter_t::enqueue_remove_peer(peer_id_t peer_id) {
  // If router thread is not running, process synchronously (for tests)
  if (!router_running.load()) {
    remove_peer(peer_id);
    return true;
  }
  
  rtpmidid::routing_request_t request;
  request.command = rtpmidid::routing_command_e::REMOVE_PEER;
  request.from_peer_id = peer_id;
  request.to_peer_id = 0;

  const bool enqueued = routing_queue.enqueue(std::move(request));
  if (!enqueued) {
    WARNING("Routing queue full, dropping remove peer request");
    return false;
  }
  router_wakeup.notify_one();
  return true;
}

bool midirouter_t::enqueue_event(peer_id_t from, peer_id_t to, 
                                 midipeer_event_e evt) {
  // If router thread is not running, process synchronously (for tests)
  if (!router_running.load()) {
    if (to == 0) {
      event(from, evt);
    } else {
      event(from, to, evt);
    }
    return true;
  }
  
  rtpmidid::routing_request_t request;
  request.command = rtpmidid::routing_command_e::PEER_EVENT;
  request.from_peer_id = from;
  request.to_peer_id = to;
  request.data.push_back(static_cast<uint8_t>(evt));

  const bool enqueued = routing_queue.enqueue(std::move(request));
  if (!enqueued) {
    WARNING("Routing queue full, dropping event");
    return false;
  }
  router_wakeup.notify_one();
  return true;
}

bool midirouter_t::enqueue_event(peer_id_t from, midipeer_event_e evt) {
  return enqueue_event(from, 0, evt);
}

} // namespace rtpmididns

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
#include "json_fwd.hpp"
#include "midipeer.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/utils.hpp"
#include "rtpmidid/lockfree_queue.hpp"
#include "rtpmidid/threading_types.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rtpmididns {
class mididata_t;
class midipeer_t;

using peer_id_t = uint32_t;

struct peerconnection_t {
  uint32_t id = 0;
  std::shared_ptr<midipeer_t> peer;
  std::vector<peer_id_t> send_to{};
};

class midirouter_t : public std::enable_shared_from_this<midirouter_t> {
  NON_COPYABLE_NOR_MOVABLE(midirouter_t)

private:
  static constexpr size_t ROUTING_QUEUE_SIZE = 4096;
  rtpmidid::mpsc_queue<rtpmidid::routing_request_t, ROUTING_QUEUE_SIZE> routing_queue;
  
  // Router thread management
  std::thread router_thread;
  std::atomic<bool> router_running{false};
  std::condition_variable router_wakeup;
  std::mutex router_mutex;
  
  // Thread-safe peer map access
  mutable std::shared_mutex peers_mutex;
  
  // Track peers being removed to avoid recursive removal
  std::set<peer_id_t> removing_peers;
  std::mutex removing_peers_mutex;
  
  // Function to enqueue MIDI to destination peer (set by peer threads)
  std::function<void(peer_id_t, const rtpmidid::midi_packet_t&)> peer_enqueue_fn;

  // Router thread main loop
  void router_thread_loop();

public:
  peer_id_t max_id = 1;
  std::unordered_map<uint32_t, peerconnection_t> peers;
  midirouter_t();
  ~midirouter_t();
  
  // Thread-safe methods (can be called from any thread)
  void start_router_thread();
  void stop_router_thread();
  
  // Set function to enqueue MIDI to peer threads
  void set_peer_enqueue_function(
      std::function<void(peer_id_t, const rtpmidid::midi_packet_t&)> fn) {
    peer_enqueue_fn = std::move(fn);
  }
  
  // Thread-safe enqueue methods (non-blocking)
  bool enqueue_send_midi(peer_id_t from, const mididata_t &data);
  bool enqueue_connect(peer_id_t from, peer_id_t to);
  bool enqueue_disconnect(peer_id_t from, peer_id_t to);
  bool enqueue_remove_peer(peer_id_t peer_id);
  bool enqueue_event(peer_id_t from, peer_id_t to, midipeer_event_e evt);
  bool enqueue_event(peer_id_t from, midipeer_event_e evt);

  peer_id_t add_peer(std::shared_ptr<midipeer_t>);
  std::shared_ptr<midipeer_t> get_peer_by_id(peer_id_t peer_id);
  peerconnection_t *get_peerdata_by_id(peer_id_t peer_id);

  /** Thread-safe peer map size (use instead of `peers.size()` from other threads). */
  size_t peer_count() const;
  /** Thread-safe copy of `send_to` for `from` (empty if unknown). */
  std::vector<peer_id_t> send_targets_for(peer_id_t from) const;

  void remove_peer(peer_id_t);
  void connect(peer_id_t from, peer_id_t to);
  void disconnect(peer_id_t from, peer_id_t to);
  void peer_connection_loop(peer_id_t peer_id,
                            std::function<void(std::shared_ptr<midipeer_t>)>);

  void send_midi(peer_id_t from, const mididata_t &data);
  void send_midi(peer_id_t from, peer_id_t to, const mididata_t &data);
  // Specific from one peer to another
  void event(peer_id_t from, peer_id_t to, midipeer_event_e event);
  // From one peer to all connected others
  void event(peer_id_t from, midipeer_event_e event);

  // To force clear the peers and avoid the cyclic references of peers that keep
  // the router.
  void clear();

  json_t status();

  // For the given type of the for_each, by default midipeer_t.
  template <typename T = midipeer_t>
  void for_each_peer(const std::function<void(T *)> &f) {
    for (auto &[peer_id, peer] : peers) {
      auto t = dynamic_cast<T *>(peer.peer.get());
      if (t)
        f(t);
    }
  };
};
} // namespace rtpmididns

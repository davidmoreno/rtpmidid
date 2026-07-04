/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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
#include "rtpmidid/shutdown_signals.hpp"
#include "webui_midi_monitor_peer.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <variant>

namespace rtpmididns {

namespace {

/**
 * Per-thread tag identifying which router (if any) owns the running thread.
 * Used by `on_router_thread()` so synchronous, queue-bypass execution kicks in
 * from inside signal listeners and queue handlers.
 */
thread_local midirouter_t *g_current_router_thread = nullptr;

bool is_web_ephemeral_display_name(const std::string &name) {
  if (name.starts_with("WEB · MIDI monitor"))
    return false;
  return name.starts_with("WEB · ") || name.starts_with("WEB:");
}

/** Returns true if the peer has an active network session (RTP connected). */
bool has_active_network_session(const midipeer_t &peer) {
  const auto row = peer.status();
  if (row.peer && row.peer->status == "3")
    return true;
  if (row.peers) {
    for (const auto &p : *row.peers) {
      if (p.status == "3")
        return true;
    }
  }
  return false;
}

} // namespace

midirouter_t::midirouter_t() = default;
midirouter_t::~midirouter_t() { stop_router_thread(); }

// ===========================================================================
// Threading helpers
// ===========================================================================

bool midirouter_t::on_router_thread() const {
  return g_current_router_thread == this;
}

bool midirouter_t::sync_mode() const { return !router_running_.load(); }

bool midirouter_t::enqueue(router_command_t &&cmd,
                           rtpmidid::queue_priority_e prio) {
  if (!queue_.enqueue(std::move(cmd), prio)) {
    WARNING("component=router Queue full, dropping command (priority={})",
            static_cast<int>(prio));
    return false;
  }
  return true;
}

void midirouter_t::post_signal_peer_added(peer_id_t peer_id) {
  if (sync_mode()) {
    peer_added_event(peer_id);
    return;
  }
  enqueue(router_command_t{router_cmd::signal_peer_added_t{peer_id}},
          rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::post_signal_peer_removed(peer_id_t peer_id) {
  if (sync_mode()) {
    peer_removed_event(peer_id);
    return;
  }
  enqueue(router_command_t{router_cmd::signal_peer_removed_t{peer_id}},
          rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::post_signal_connected(peer_id_t from, peer_id_t to) {
  if (sync_mode()) {
    connected_event(from, to);
    return;
  }
  enqueue(router_command_t{router_cmd::signal_connected_t{from, to}},
          rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::post_signal_disconnected(peer_id_t from, peer_id_t to) {
  if (sync_mode()) {
    disconnected_event(from, to);
    return;
  }
  enqueue(router_command_t{router_cmd::signal_disconnected_t{from, to}},
          rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::post_signal_peer_event(peer_id_t peer_id,
                                          midipeer_event_e evt) {
  if (sync_mode()) {
    peer_event(peer_id, evt);
    return;
  }
  enqueue(router_command_t{router_cmd::signal_peer_event_t{peer_id, evt}},
          rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::dispatch_for_each_peer(
    std::function<void(midirouter_t &)> task) {
  if (!task)
    return;
  if (sync_mode() || on_router_thread()) {
    task(*this);
    return;
  }
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  const uint64_t id = channel->next_id();
  router_cmd::for_each_peer_t cmd;
  cmd.task = std::move(task);
  cmd.reply = rtpmidid::reply_slot_t{channel, id};
  if (!enqueue(router_command_t{std::move(cmd)},
               rtpmidid::queue_priority_e::LOW)) {
    return;
  }
  auto env = channel->wait(id, kReplyTimeout);
  if (!env.error.empty()) {
    WARNING("for_each_peer: reply error '{}'", env.error);
  }
}

// ===========================================================================
// Inline implementations (router-thread-owned state)
// ===========================================================================

peer_id_t midirouter_t::add_peer_impl(std::shared_ptr<midipeer_t> peer) {
  if (peer->peer_id) {
    WARNING("Peer already present!");
    return peer->peer_id;
  }

  const auto pid = max_id_++;
  peer->peer_id = pid;
  try {
    peer->router = shared_from_this();
  } catch (const std::exception &exc) {
    ERROR("Error on SHARED FROM THIS! Make sure that the router is a "
          "std::shared_ptr<midirouter_t>. {} {}",
          (void *)this, exc.what());
    throw;
  }

  peers_[pid] = peerconnection_t{pid, peer, {}};
  INFO("peer_id={} component=router Added peer type={} peer_id={}", pid, peer->get_type(), pid);

  if (router_running_.load()) {
    peer->start_thread();
  }

  try {
    peer->on_router_attached();
  } catch (...) {
    remove_peer_impl(pid);
    throw;
  }

  post_signal_peer_added(pid);
  if (on_peer_registered)
    on_peer_registered(pid);
  return pid;
}

void midirouter_t::remove_peer_impl(peer_id_t peer_id) {
  INFO("peer_id={} component=router Remove peer", peer_id);

  if (removing_peers_.find(peer_id) != removing_peers_.end()) {
    WARNING("peer_id={} component=router Already removing peer, skipping recursive removal", peer_id);
    return;
  }
  removing_peers_.insert(peer_id);

  auto toremove = peers_.find(peer_id);
  if (toremove == peers_.end()) {
    removing_peers_.erase(peer_id);
    return;
  }

  auto peer_ptr = toremove->second.peer;
  const bool is_monitor =
      std::dynamic_pointer_cast<webui_midi_monitor_peer_t>(peer_ptr) != nullptr;
  if (is_monitor) {
    auto mon = std::dynamic_pointer_cast<webui_midi_monitor_peer_t>(peer_ptr);
    mon->clear_ws_binary_sink();
    monitor_registry_unregister(mon->session_uuid());
  }

  try {
    peer_ptr->stop_thread();
  } catch (const std::exception &e) {
    ERROR("peer_id={} component=router Exception stopping peer thread: {}", peer_id, e.what());
  }

  toremove = peers_.find(peer_id);
  if (toremove == peers_.end()) {
    removing_peers_.erase(peer_id);
    return;
  }

  // Tear down outgoing edges (fires DISCONNECTED_ROUTER like disconnect()).
  std::vector<peer_id_t> outgoing;
  {
    outgoing = toremove->second.send_to;
    for (auto to_id : outgoing) {
      disconnect_impl(peer_id, to_id);
    }
  }

  std::vector<peer_id_t> inbound_from;
  for (const auto &p : peers_) {
    if (p.first == peer_id)
      continue;
    for (auto to : p.second.send_to) {
      if (to == peer_id) {
        inbound_from.push_back(p.first);
        break;
      }
    }
  }
  for (auto from_id : inbound_from) {
    disconnect_impl(from_id, peer_id);
  }

  toremove = peers_.find(peer_id);
  if (toremove == peers_.end()) {
    removing_peers_.erase(peer_id);
    return;
  }

  toremove->second.peer->router = nullptr;
  const auto removed = peers_.erase(peer_id);
  if (removed) {
    INFO("peer_id={} component=router Removed peer", peer_id);
    post_signal_peer_removed(peer_id);
    if (on_peer_unregistered)
      on_peer_unregistered(peer_id);
  }

  // Clean up peers that were connected to the removed peer and are now
  // isolated (e.g. a device connected only to a monitor).
  {
    std::set<peer_id_t> affected(outgoing.begin(), outgoing.end());
    affected.insert(inbound_from.begin(), inbound_from.end());
    for (auto aff_id : affected) {
      if (aff_id == peer_id) continue;
      maybe_remove_ephemeral_web_peer(aff_id);
    }
    // When a monitor peer is removed, also clean up device peers that
    // were connected to it and are now isolated with an active RTP session.
    if (is_monitor) {
      for (auto aff_id : affected) {
        if (aff_id == peer_id) continue;
        auto it = peers_.find(aff_id);
        if (it == peers_.end()) continue;
        if (!peer_is_router_isolated(aff_id)) continue;
        if (!has_active_network_session(*it->second.peer)) continue;
        INFO("peer_id={} component=router Cleaning up isolated network peer "
             "after monitor removal", aff_id);
        remove_peer_impl(aff_id);
      }
    }
  }

  removing_peers_.erase(peer_id);
}

void midirouter_t::remove_all_peers_impl() {
  while (!peers_.empty()) {
    const auto id = peers_.begin()->first;
    remove_peer_impl(id);
  }
}

void midirouter_t::connect_impl(peer_id_t from, peer_id_t to) {
  auto from_it = peers_.find(from);
  auto to_it = peers_.find(to);
  if (from_it == peers_.end() || to_it == peers_.end()) {
    WARNING("component=router connect: unknown peer {} -> {}", from, to);
    return;
  }

  auto &from_peer = from_it->second;
  auto &to_peer = to_it->second;

  for (auto existing : from_peer.send_to) {
    if (existing == to)
      return;
  }

  from_peer.send_to.push_back(to);

  from_peer.peer->event(midipeer_event_e::CONNECTED_ROUTER, to);
  to_peer.peer->event(midipeer_event_e::CONNECTED_ROUTER, from);

  INFO("component=router Connect {} -> {}", from, to);
  post_signal_connected(from, to);
}

void midirouter_t::disconnect_impl(peer_id_t from, peer_id_t to) {
  auto from_it = peers_.find(from);
  auto to_it = peers_.find(to);
  if (from_it == peers_.end() || to_it == peers_.end()) {
    WARNING("component=router disconnect: unknown peer {} -> {}", from, to);
    return;
  }

  auto &from_peer = from_it->second;
  auto &to_peer = to_it->second;

  for (auto it = from_peer.send_to.begin(); it != from_peer.send_to.end();
       ++it) {
    if (*it == to) {
      from_peer.send_to.erase(it);
      from_peer.peer->event(midipeer_event_e::DISCONNECTED_ROUTER, to);
      to_peer.peer->event(midipeer_event_e::DISCONNECTED_ROUTER, from);
      INFO("component=router Disconnect {} -> {}", from, to);
      post_signal_disconnected(from, to);
      return;
    }
  }
}

bool midirouter_t::peer_is_router_isolated(peer_id_t id) const {
  const auto it = peers_.find(id);
  if (it == peers_.end())
    return false;
  if (!it->second.send_to.empty())
    return false;
  for (const auto &p : peers_) {
    if (p.first == id)
      continue;
    for (auto to : p.second.send_to) {
      if (to == id)
        return false;
    }
  }
  return true;
}

void midirouter_t::remove_monitors_targeting(peer_id_t target) {
  std::vector<peer_id_t> monitor_ids;
  for (const auto &kv : peers_) {
    const auto mon =
        std::dynamic_pointer_cast<webui_midi_monitor_peer_t>(kv.second.peer);
    if (!mon)
      continue;
    if (mon->monitor_target_peer_id() == target)
      monitor_ids.push_back(kv.first);
  }
  for (peer_id_t mid : monitor_ids)
    remove_peer_impl(mid);
}

void midirouter_t::maybe_remove_ephemeral_web_peer(peer_id_t id) {
  if (!peer_is_router_isolated(id))
    return;
  const auto it = peers_.find(id);
  if (it == peers_.end())
    return;
  const auto row = it->second.peer->status();
  const std::string name = row.name.value_or("");
  if (!is_web_ephemeral_display_name(name))
    return;
  remove_monitors_targeting(id);
  if (peers_.find(id) == peers_.end())
    return;
  if (!peer_is_router_isolated(id))
    return;
  remove_peer_impl(id);
}

void midirouter_t::send_midi_inline(peer_id_t from, peer_id_t to,
                                    const uint8_t *data, size_t size) {
  auto from_it = peers_.find(from);
  if (from_it == peers_.end()) {
    WARNING("component=router Sending from an unknown peer {}!", from);
    return;
  }

  if (on_peer_sent)
    on_peer_sent(from);

  mididata_t mididata(const_cast<uint8_t *>(data),
                      static_cast<uint32_t>(size));

  if (to != 0) {
    auto to_it = peers_.find(to);
    if (to_it == peers_.end()) {
      WARNING("component=router Sending to unknown peer {} -> {}", from, to);
      return;
    }
    to_it->second.peer->send_midi(from, mididata);
    if (on_peer_recv)
      on_peer_recv(to);
    return;
  }

  for (auto to_id : from_it->second.send_to) {
    auto to_it = peers_.find(to_id);
    if (to_it == peers_.end())
      continue;
    to_it->second.peer->send_midi(from, mididata);
    if (on_peer_recv)
      on_peer_recv(to_id);
  }
}

void midirouter_t::event_directed_impl(peer_id_t from, peer_id_t to,
                                       midipeer_event_e evt) {
  auto peer_it = peers_.find(to);
  if (peer_it == peers_.end())
    return;
  peer_it->second.peer->event(evt, from);
  post_signal_peer_event(to, evt);
}

void midirouter_t::event_broadcast_impl(peer_id_t from, midipeer_event_e evt) {
  auto peer_it = peers_.find(from);
  if (peer_it == peers_.end())
    return;
  post_signal_peer_event(from, evt);
  for (auto to_id : peer_it->second.send_to) {
    auto topeer_it = peers_.find(to_id);
    if (topeer_it == peers_.end())
      continue;
    topeer_it->second.peer->event(evt, from);
    post_signal_peer_event(to_id, evt);
  }
}

std::vector<router_peer_row_t> midirouter_t::status_rows_impl() {
  std::vector<router_peer_row_t> routerdata;
  routerdata.reserve(peers_.size());

  // --- Phase 1: dispatch all latency queries in parallel ---
  // Each peer has its own thread + queue, so issuing one query per peer up
  // front lets them compute their stats concurrently while we wait.
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  std::vector<std::pair<peer_id_t, uint64_t>> pending;
  pending.reserve(peers_.size());

  for (const auto &kv : peers_) {
    auto &peer = kv.second.peer;
    if (!peer)
      continue;
    const uint64_t id = channel->next_id();
    if (peer->request_internal_latency_stats(channel, id)) {
      pending.emplace_back(kv.first, id);
    }
  }

  // --- Phase 2: collect replies under a shared deadline ---
  std::unordered_map<peer_id_t, internal_latency_ms_t> latency_by_id;
  const auto deadline =
      std::chrono::steady_clock::now() + kStatusLatencyBudget;
  for (const auto &p : pending) {
    auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::milliseconds(0))
      break;
    auto env = channel->wait(
        p.second,
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining));
    if (!env.error.empty())
      continue;
    try {
      latency_by_id.emplace(
          p.first, std::any_cast<internal_latency_ms_t>(env.value));
    } catch (const std::bad_any_cast &) {
    }
  }

  // --- Phase 3: build rows (status() is a virtual called inline) ---
  for (const auto &kv : peers_) {
    try {
      auto row = kv.second.peer->status();
      row.id = kv.first;
      row.send_to = kv.second.send_to;
      row.type = kv.second.peer->get_type();
      peer_stats_t st;
      st.recv = get_stats_recv ? get_stats_recv(kv.first) : 0;
      st.sent = get_stats_sent ? get_stats_sent(kv.first) : 0;
      row.stats = st;

      auto it = latency_by_id.find(kv.first);
      if (it != latency_by_id.end()) {
        row.internal_latency_ms = it->second;
      }
      routerdata.push_back(std::move(row));
    } catch (const std::exception &exc) {
      router_peer_row_t row{};
      row.error = exc.what();
      routerdata.push_back(std::move(row));
    }
  }
  return routerdata;
}

// ===========================================================================
// Variant dispatch handlers
// ===========================================================================

void midirouter_t::handle(router_cmd::send_midi_t &cmd) {
  auto from_it = peers_.find(cmd.from);
  if (from_it == peers_.end()) {
    WARNING("component=router send_midi: unknown source peer {}", cmd.from);
    return;
  }

  // Notify stats collector — lock-free SPSC push, no allocation, no blocking
  if (on_peer_sent)
    on_peer_sent(cmd.from);

  rtpmidid::midi_packet_t packet(cmd.from, cmd.data.data(), cmd.data.size());

  if (cmd.to != 0) {
    auto to_it = peers_.find(cmd.to);
    if (to_it != peers_.end()) {
      to_it->second.peer->enqueue_midi_packet(packet);
      if (on_peer_recv)
        on_peer_recv(cmd.to);
    }
    return;
  }

  for (auto to : from_it->second.send_to) {
    auto to_it = peers_.find(to);
    if (to_it != peers_.end()) {
      to_it->second.peer->enqueue_midi_packet(packet);
      if (on_peer_recv)
        on_peer_recv(to);
    }
  }
}

void midirouter_t::handle(router_cmd::add_peer_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  try {
    env.value = std::any(add_peer_impl(std::move(cmd.peer)));
  } catch (const std::exception &exc) {
    env.error = exc.what();
  } catch (...) {
    env.error = "unknown exception in add_peer";
  }
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::remove_peer_t &cmd) {
  remove_peer_impl(cmd.id);
}

void midirouter_t::handle(router_cmd::remove_all_peers_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  try {
    remove_all_peers_impl();
  } catch (const std::exception &exc) {
    env.error = exc.what();
  } catch (...) {
    env.error = "unknown exception in remove_all_peers";
  }
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::connect_t &cmd) {
  connect_impl(cmd.from, cmd.to);
}

void midirouter_t::handle(router_cmd::disconnect_t &cmd) {
  disconnect_impl(cmd.from, cmd.to);
  maybe_remove_ephemeral_web_peer(cmd.from);
  maybe_remove_ephemeral_web_peer(cmd.to);
}

void midirouter_t::handle(router_cmd::event_directed_t &cmd) {
  event_directed_impl(cmd.from, cmd.to, cmd.evt);
}

void midirouter_t::handle(router_cmd::event_broadcast_t &cmd) {
  event_broadcast_impl(cmd.from, cmd.evt);
}

void midirouter_t::handle(router_cmd::signal_peer_added_t &cmd) {
  try {
    peer_added_event(cmd.peer_id);
  } catch (const std::exception &exc) {
    ERROR("signal_peer_added listener: {}", exc.what());
  }
}

void midirouter_t::handle(router_cmd::signal_peer_removed_t &cmd) {
  try {
    peer_removed_event(cmd.peer_id);
  } catch (const std::exception &exc) {
    ERROR("signal_peer_removed listener: {}", exc.what());
  }
}

void midirouter_t::handle(router_cmd::signal_connected_t &cmd) {
  try {
    connected_event(cmd.from, cmd.to);
  } catch (const std::exception &exc) {
    ERROR("signal_connected listener: {}", exc.what());
  }
}

void midirouter_t::handle(router_cmd::signal_disconnected_t &cmd) {
  try {
    disconnected_event(cmd.from, cmd.to);
  } catch (const std::exception &exc) {
    ERROR("signal_disconnected listener: {}", exc.what());
  }
}

void midirouter_t::handle(router_cmd::signal_peer_event_t &cmd) {
  try {
    peer_event(cmd.peer_id, cmd.evt);
  } catch (const std::exception &exc) {
    ERROR("signal_peer_event listener: {}", exc.what());
  }
}

void midirouter_t::handle(router_cmd::query_peer_count_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  env.value = std::any(peers_.size());
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::query_peer_ids_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  std::vector<peer_id_t> ids;
  ids.reserve(peers_.size());
  for (const auto &p : peers_)
    ids.push_back(p.first);
  env.value = std::any(std::move(ids));
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::query_send_targets_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  std::vector<peer_id_t> targets;
  auto it = peers_.find(cmd.from);
  if (it != peers_.end()) {
    targets = it->second.send_to;
  }
  env.value = std::any(std::move(targets));
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::query_get_peer_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  std::shared_ptr<midipeer_t> peer;
  auto it = peers_.find(cmd.peer_id);
  if (it != peers_.end()) {
    peer = it->second.peer;
  }
  env.value = std::any(std::move(peer));
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::query_status_rows_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  try {
    env.value = std::any(status_rows_impl());
  } catch (const std::exception &exc) {
    env.error = exc.what();
  } catch (...) {
    env.error = "unknown exception in status_rows";
  }
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::for_each_peer_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  try {
    if (cmd.task)
      cmd.task(*this);
  } catch (const std::exception &exc) {
    env.error = exc.what();
  } catch (...) {
    env.error = "unknown exception in for_each_peer";
  }
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::peer_connection_loop_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  try {
    auto it = peers_.find(cmd.peer_id);
    if (it == peers_.end()) {
      WARNING("peer_connection_loop: unknown peer {}!", cmd.peer_id);
    } else {
      const auto send_to = it->second.send_to;
      for (auto to : send_to) {
        auto it2 = peers_.find(to);
        if (it2 != peers_.end() && cmd.func)
          cmd.func(it2->second.peer);
      }
    }
  } catch (const std::exception &exc) {
    env.error = exc.what();
  } catch (...) {
    env.error = "unknown exception in peer_connection_loop";
  }
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::shutdown_t & /*cmd*/) {
  router_running_.store(false);
}

// ===========================================================================
// Router thread loop
// ===========================================================================

void midirouter_t::router_thread_loop() {
  rtpmidid::block_shutdown_signals();
  g_current_router_thread = this;

  router_command_t cmd;
  while (router_running_.load()) {
    if (queue_.wait_dequeue(cmd))
      std::visit([this](auto &c) { this->handle(c); }, cmd);
    // wait_dequeue returned false → heartbeat timeout; re-check router_running_
  }

  // Drain remaining commands so reply channels never deadlock.
  while (queue_.try_dequeue(cmd))
    std::visit([this](auto &c) { this->handle(c); }, cmd);

  g_current_router_thread = nullptr;
}

void midirouter_t::start_router_thread() {
  if (router_running_.exchange(true))
    return;
  router_thread_ = std::thread(&midirouter_t::router_thread_loop, this);
}

void midirouter_t::stop_router_thread() {
  if (!router_running_.load())
    return;
  // Message-driven shutdown: the handler flips router_running_ on the
  // router thread, then the loop exits and drains. wake() is a safety
  // net for the (rare) case where the queue is full and the enqueue
  // fails — the consumer still re-checks router_running_ on its next
  // heartbeat.
  if (!enqueue(router_command_t{router_cmd::shutdown_t{}},
               rtpmidid::queue_priority_e::NORMAL)) {
    router_running_.store(false);
    queue_.wake();
  }
  if (router_thread_.joinable())
    router_thread_.join();
}

void midirouter_t::drain_for_tests() {
  router_command_t cmd;
  while (queue_.try_dequeue(cmd)) {
    std::visit([this](auto &c) { this->handle(c); }, cmd);
  }
}

// ===========================================================================
// Public API — thin wrappers that dispatch through the queue
// ===========================================================================

peer_id_t midirouter_t::add_peer(std::shared_ptr<midipeer_t> peer) {
  if (sync_mode() || on_router_thread()) {
    return add_peer_impl(std::move(peer));
  }
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  const uint64_t id = channel->next_id();
  router_cmd::add_peer_t cmd;
  cmd.peer = std::move(peer);
  cmd.reply = rtpmidid::reply_slot_t{channel, id};
  if (!enqueue(router_command_t{std::move(cmd)},
               rtpmidid::queue_priority_e::NORMAL)) {
    return 0;
  }
  auto env = channel->wait(id, kReplyTimeout);
  if (!env.error.empty()) {
    ERROR("add_peer: reply error '{}'", env.error);
    return 0;
  }
  try {
    return std::any_cast<peer_id_t>(env.value);
  } catch (const std::bad_any_cast &) {
    return 0;
  }
}

void midirouter_t::remove_peer(peer_id_t peer_id) {
  if (sync_mode() || on_router_thread()) {
    remove_peer_impl(peer_id);
    return;
  }
  router_cmd::remove_peer_t cmd{peer_id};
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::connect(peer_id_t from, peer_id_t to) {
  if (sync_mode() || on_router_thread()) {
    connect_impl(from, to);
    return;
  }
  router_cmd::connect_t cmd{from, to};
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::connect_blocking(peer_id_t from, peer_id_t to) {
  connect(from, to);
  if (sync_mode() || on_router_thread())
    return;
  const auto deadline =
      std::chrono::steady_clock::now() + kReplyTimeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto targets = send_targets_for(from);
    if (std::find(targets.begin(), targets.end(), to) != targets.end())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  throw std::runtime_error(
      FMT::format("Timed out waiting for router connect {} -> {}", from, to));
}

void midirouter_t::disconnect(peer_id_t from, peer_id_t to) {
  if (sync_mode() || on_router_thread()) {
    disconnect_impl(from, to);
    return;
  }
  router_cmd::disconnect_t cmd{from, to};
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::send_midi(peer_id_t from, const mididata_t &data) {
  if (sync_mode()) {
    send_midi_inline(from, 0, data.position, data.remaining());
    return;
  }
  router_cmd::send_midi_t cmd;
  cmd.from = from;
  cmd.to = 0;
  cmd.data.assign(data.position, data.position + data.remaining());
  if (on_router_thread()) {
    handle(cmd);
    return;
  }
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::HIGH);
}

void midirouter_t::send_midi(peer_id_t from, peer_id_t to,
                             const mididata_t &data) {
  if (sync_mode()) {
    send_midi_inline(from, to, data.position, data.remaining());
    return;
  }
  router_cmd::send_midi_t cmd;
  cmd.from = from;
  cmd.to = to;
  cmd.data.assign(data.position, data.position + data.remaining());
  if (on_router_thread()) {
    handle(cmd);
    return;
  }
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::HIGH);
}

void midirouter_t::event(peer_id_t from, peer_id_t to, midipeer_event_e evt) {
  if (sync_mode() || on_router_thread()) {
    event_directed_impl(from, to, evt);
    return;
  }
  router_cmd::event_directed_t cmd{from, to, evt};
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::event(peer_id_t from, midipeer_event_e evt) {
  if (sync_mode() || on_router_thread()) {
    event_broadcast_impl(from, evt);
    return;
  }
  router_cmd::event_broadcast_t cmd{from, evt};
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::remove_all_peers() {
  if (sync_mode() || on_router_thread()) {
    remove_all_peers_impl();
    return;
  }
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  const uint64_t id = channel->next_id();
  router_cmd::remove_all_peers_t cmd;
  cmd.reply = rtpmidid::reply_slot_t{channel, id};
  if (!enqueue(router_command_t{std::move(cmd)},
               rtpmidid::queue_priority_e::NORMAL)) {
    return;
  }
  auto env = channel->wait(id, kReplyTimeout);
  if (!env.error.empty()) {
    WARNING("remove_all_peers: reply error '{}'", env.error);
  }
}

void midirouter_t::clear() { remove_all_peers(); }

// ===========================================================================
// Read API — typed query messages
// ===========================================================================

std::shared_ptr<midipeer_t> midirouter_t::get_peer_by_id(peer_id_t peer_id) {
  return dispatch_query<std::shared_ptr<midipeer_t>>(
      router_cmd::query_get_peer_t{peer_id, {}},
      rtpmidid::queue_priority_e::LOW,
      [peer_id](midirouter_t &r) -> std::shared_ptr<midipeer_t> {
        auto it = r.peers_.find(peer_id);
        if (it == r.peers_.end())
          return nullptr;
        return it->second.peer;
      });
}

size_t midirouter_t::peer_count() const {
  return dispatch_query<size_t>(
      router_cmd::query_peer_count_t{}, rtpmidid::queue_priority_e::LOW,
      [](midirouter_t &r) -> size_t { return r.peers_.size(); });
}

std::vector<peer_id_t> midirouter_t::peer_ids() const {
  return dispatch_query<std::vector<peer_id_t>>(
      router_cmd::query_peer_ids_t{}, rtpmidid::queue_priority_e::LOW,
      [](midirouter_t &r) -> std::vector<peer_id_t> {
        std::vector<peer_id_t> ids;
        ids.reserve(r.peers_.size());
        for (const auto &p : r.peers_)
          ids.push_back(p.first);
        return ids;
      });
}

std::vector<peer_id_t> midirouter_t::send_targets_for(peer_id_t from) const {
  return dispatch_query<std::vector<peer_id_t>>(
      router_cmd::query_send_targets_t{from, {}},
      rtpmidid::queue_priority_e::LOW,
      [from](midirouter_t &r) -> std::vector<peer_id_t> {
        auto it = r.peers_.find(from);
        if (it == r.peers_.end())
          return {};
        return it->second.send_to;
      });
}

std::vector<router_peer_row_t> midirouter_t::status_rows() const {
  return dispatch_query<std::vector<router_peer_row_t>>(
      router_cmd::query_status_rows_t{}, rtpmidid::queue_priority_e::LOW,
      [](midirouter_t &r) { return r.status_rows_impl(); });
}

std::optional<router_peer_row_t>
midirouter_t::status_row_for(peer_id_t peer_id) const {
  for (const auto &row : status_rows()) {
    if (row.id && static_cast<peer_id_t>(*row.id) == peer_id)
      return row;
  }
  return std::nullopt;
}

void midirouter_t::peer_connection_loop(
    peer_id_t peer_id,
    std::function<void(std::shared_ptr<midipeer_t>)> func) {
  if (sync_mode() || on_router_thread()) {
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
      WARNING("peer_connection_loop: unknown peer {}!", peer_id);
      return;
    }
    const auto send_to = it->second.send_to;
    for (auto to : send_to) {
      auto it2 = peers_.find(to);
      if (it2 != peers_.end())
        func(it2->second.peer);
    }
    return;
  }
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  const uint64_t id = channel->next_id();
  router_cmd::peer_connection_loop_t cmd;
  cmd.peer_id = peer_id;
  cmd.func = std::move(func);
  cmd.reply = rtpmidid::reply_slot_t{channel, id};
  if (!enqueue(router_command_t{std::move(cmd)},
               rtpmidid::queue_priority_e::LOW)) {
    return;
  }
  auto env = channel->wait(id, kReplyTimeout);
  if (!env.error.empty()) {
    WARNING("peer_connection_loop: reply error '{}'", env.error);
  }
}

} // namespace rtpmididns

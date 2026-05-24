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
#include <chrono>
#include <variant>

namespace rtpmididns {

namespace {

/**
 * Per-thread tag identifying which router (if any) owns the running thread.
 * Used by `on_router_thread()` so synchronous, queue-bypass execution kicks in
 * from inside signal listeners and queue handlers.
 */
thread_local midirouter_t *g_current_router_thread = nullptr;

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
  const bool ok = queue_.enqueue(std::move(cmd), prio);
  if (!ok) {
    WARNING("Router queue full, dropping command (priority={})",
            static_cast<int>(prio));
    return false;
  }
  router_wakeup_.notify_one();
  return true;
}

void midirouter_t::post_signal(std::function<void(midirouter_t &)> task) {
  if (!task)
    return;
  if (sync_mode()) {
    task(*this);
    return;
  }
  router_cmd::fire_signal_t cmd{std::move(task)};
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::submit_run(rtpmidid::queue_priority_e prio,
                              std::function<void(midirouter_t &)> task) {
  if (!task)
    return;
  if (sync_mode() || on_router_thread()) {
    task(*this);
    return;
  }
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  const uint64_t id = channel->next_id();
  router_cmd::run_task_t cmd;
  cmd.task = std::move(task);
  cmd.reply = rtpmidid::reply_slot_t{channel, id};
  if (!enqueue(router_command_t{std::move(cmd)}, prio)) {
    return;
  }
  auto env = channel->wait(id, kReplyTimeout);
  if (!env.error.empty()) {
    WARNING("submit_run: reply error '{}'", env.error);
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
  INFO("Added peer type={} peer_id={}", peer->get_type(), pid);

  if (router_running_.load()) {
    peer->start_thread();
  }

  peer->on_router_attached();

  post_signal([pid](midirouter_t &r) { r.peer_added_event(pid); });
  return pid;
}

void midirouter_t::remove_peer_impl(peer_id_t peer_id) {
  INFO("Remove peer_id={}", peer_id);

  if (removing_peers_.find(peer_id) != removing_peers_.end()) {
    WARNING("Already removing peer {}, skipping recursive removal", peer_id);
    return;
  }
  removing_peers_.insert(peer_id);

  auto toremove = peers_.find(peer_id);
  if (toremove == peers_.end()) {
    removing_peers_.erase(peer_id);
    return;
  }

  auto peer_ptr = toremove->second.peer;
  if (auto mon = std::dynamic_pointer_cast<webui_midi_monitor_peer_t>(peer_ptr)) {
    mon->clear_ws_binary_sink();
    monitor_registry_unregister(mon->session_uuid());
  }

  try {
    peer_ptr->stop_thread();
  } catch (const std::exception &e) {
    ERROR("Exception stopping peer thread: {}", e.what());
  }

  toremove = peers_.find(peer_id);
  if (toremove == peers_.end()) {
    removing_peers_.erase(peer_id);
    return;
  }

  // Tear down outgoing edges (fires DISCONNECTED_ROUTER like disconnect()).
  {
    const auto outgoing = toremove->second.send_to;
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
  if (removed)
    INFO("Removed peer {}", peer_id);

  removing_peers_.erase(peer_id);
}

void midirouter_t::connect_impl(peer_id_t from, peer_id_t to) {
  auto from_it = peers_.find(from);
  auto to_it = peers_.find(to);
  if (from_it == peers_.end() || to_it == peers_.end()) {
    WARNING("connect: unknown peer {} -> {}", from, to);
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

  INFO("Connect {} -> {}", from, to);
  post_signal([from, to](midirouter_t &r) { r.connected_event(from, to); });
}

void midirouter_t::disconnect_impl(peer_id_t from, peer_id_t to) {
  auto from_it = peers_.find(from);
  auto to_it = peers_.find(to);
  if (from_it == peers_.end() || to_it == peers_.end()) {
    WARNING("disconnect: unknown peer {} -> {}", from, to);
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
      INFO("Disconnect {} -> {}", from, to);
      post_signal(
          [from, to](midirouter_t &r) { r.disconnected_event(from, to); });
      return;
    }
  }
}

void midirouter_t::send_midi_inline(peer_id_t from, peer_id_t to,
                                    const uint8_t *data, size_t size) {
  auto from_it = peers_.find(from);
  if (from_it == peers_.end()) {
    WARNING("Sending from an unknown peer {}!", from);
    return;
  }
  from_it->second.peer->packets_sent++;

  mididata_t mididata(const_cast<uint8_t *>(data),
                      static_cast<uint32_t>(size));

  if (to != 0) {
    auto to_it = peers_.find(to);
    if (to_it == peers_.end()) {
      WARNING("Sending to unknown peer {} -> {}", from, to);
      return;
    }
    to_it->second.peer->packets_recv++;
    to_it->second.peer->send_midi(from, mididata);
    return;
  }

  for (auto to_id : from_it->second.send_to) {
    auto to_it = peers_.find(to_id);
    if (to_it == peers_.end())
      continue;
    to_it->second.peer->packets_recv++;
    to_it->second.peer->send_midi(from, mididata);
  }
}

void midirouter_t::event_impl(peer_id_t from, peer_id_t to,
                              midipeer_event_e evt) {
  auto peer_it = peers_.find(to);
  if (peer_it == peers_.end())
    return;
  peer_it->second.peer->event(evt, from);
  post_signal([to, evt](midirouter_t &r) { r.peer_event(to, evt); });
}

void midirouter_t::event_broadcast_impl(peer_id_t from, midipeer_event_e evt) {
  auto peer_it = peers_.find(from);
  if (peer_it == peers_.end())
    return;
  post_signal([from, evt](midirouter_t &r) { r.peer_event(from, evt); });
  for (auto to_id : peer_it->second.send_to) {
    auto topeer_it = peers_.find(to_id);
    if (topeer_it == peers_.end())
      continue;
    topeer_it->second.peer->event(evt, from);
    const auto to = to_id;
    post_signal([to, evt](midirouter_t &r) { r.peer_event(to, evt); });
  }
}

std::vector<router_peer_row_t> midirouter_t::status_rows_impl() const {
  std::vector<router_peer_row_t> routerdata;
  routerdata.reserve(peers_.size());
  for (const auto &kv : peers_) {
    try {
      auto row = kv.second.peer->status();
      row.id = kv.first;
      row.send_to = kv.second.send_to;
      row.type = kv.second.peer->get_type();
      peer_stats_t st;
      st.recv = static_cast<uint64_t>(kv.second.peer->packets_recv.load());
      st.sent = static_cast<uint64_t>(kv.second.peer->packets_sent.load());
      row.stats = st;
      row.internal_latency_ms = kv.second.peer->internal_latency_stats();
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
    WARNING("send_midi: unknown source peer {}", cmd.from);
    return;
  }
  from_it->second.peer->packets_sent++;

  rtpmidid::midi_packet_t packet(cmd.from, cmd.data.data(), cmd.data.size());

  if (cmd.to != 0) {
    auto to_it = peers_.find(cmd.to);
    if (to_it != peers_.end()) {
      to_it->second.peer->enqueue_midi_packet(packet);
    }
    return;
  }

  for (auto to : from_it->second.send_to) {
    auto to_it = peers_.find(to);
    if (to_it != peers_.end()) {
      to_it->second.peer->enqueue_midi_packet(packet);
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

void midirouter_t::handle(router_cmd::connect_t &cmd) {
  connect_impl(cmd.from, cmd.to);
}

void midirouter_t::handle(router_cmd::disconnect_t &cmd) {
  disconnect_impl(cmd.from, cmd.to);
}

void midirouter_t::handle(router_cmd::event_t &cmd) {
  if (cmd.to == 0)
    event_broadcast_impl(cmd.from, cmd.evt);
  else
    event_impl(cmd.from, cmd.to, cmd.evt);
}

void midirouter_t::handle(router_cmd::fire_signal_t &cmd) {
  if (cmd.task) {
    try {
      cmd.task(*this);
    } catch (const std::exception &exc) {
      ERROR("Exception in router signal listener: {}", exc.what());
    } catch (...) {
      ERROR("Unknown exception in router signal listener");
    }
  }
}

void midirouter_t::handle(router_cmd::run_task_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  try {
    if (cmd.task)
      cmd.task(*this);
  } catch (const std::exception &exc) {
    env.error = exc.what();
  } catch (...) {
    env.error = "unknown exception in run_task";
  }
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::query_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  try {
    if (cmd.query)
      env.value = cmd.query(*this);
  } catch (const std::exception &exc) {
    env.error = exc.what();
  } catch (...) {
    env.error = "unknown exception in query";
  }
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midirouter_t::handle(router_cmd::shutdown_t & /*cmd*/) {
  // Just used as a wakeup; the loop checks router_running_ each iteration.
}

// ===========================================================================
// Router thread loop
// ===========================================================================

void midirouter_t::router_thread_loop() {
  using namespace std::chrono_literals;
  rtpmidid::block_shutdown_signals();
  g_current_router_thread = this;

  while (router_running_.load()) {
    bool processed = false;
    router_command_t cmd;
    while (queue_.dequeue(cmd)) {
      processed = true;
      std::visit([this](auto &c) { this->handle(c); }, cmd);
    }
    if (!processed) {
      std::unique_lock<std::mutex> lk(wakeup_mutex_);
      router_wakeup_.wait_for(lk, 10ms, [this] {
        return !router_running_.load() || !queue_.empty();
      });
    }
  }

  // Drain remaining commands so reply channels never deadlock.
  router_command_t cmd;
  while (queue_.dequeue(cmd)) {
    std::visit([this](auto &c) { this->handle(c); }, cmd);
  }

  g_current_router_thread = nullptr;
}

void midirouter_t::start_router_thread() {
  if (router_running_.exchange(true))
    return;
  router_thread_ = std::thread(&midirouter_t::router_thread_loop, this);
}

void midirouter_t::stop_router_thread() {
  if (!router_running_.exchange(false))
    return;
  // Wake the loop and let it drain.
  enqueue(router_command_t{router_cmd::shutdown_t{}},
          rtpmidid::queue_priority_e::NORMAL);
  router_wakeup_.notify_all();
  if (router_thread_.joinable())
    router_thread_.join();
}

void midirouter_t::drain_for_tests() {
  router_command_t cmd;
  while (queue_.dequeue(cmd)) {
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
    event_impl(from, to, evt);
    return;
  }
  router_cmd::event_t cmd{from, to, evt};
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::event(peer_id_t from, midipeer_event_e evt) {
  if (sync_mode() || on_router_thread()) {
    event_broadcast_impl(from, evt);
    return;
  }
  router_cmd::event_t cmd{from, 0, evt};
  enqueue(router_command_t{std::move(cmd)}, rtpmidid::queue_priority_e::NORMAL);
}

void midirouter_t::remove_all_peers() {
  if (sync_mode() || on_router_thread()) {
    while (!peers_.empty()) {
      const auto id = peers_.begin()->first;
      remove_peer_impl(id);
    }
    return;
  }
  submit_run(rtpmidid::queue_priority_e::NORMAL,
             [](midirouter_t &router) {
               while (!router.peers_.empty()) {
                 const auto id = router.peers_.begin()->first;
                 router.remove_peer_impl(id);
               }
             });
}

void midirouter_t::clear() { remove_all_peers(); }

// ===========================================================================
// Read API
// ===========================================================================

std::shared_ptr<midipeer_t> midirouter_t::get_peer_by_id(peer_id_t peer_id) {
  return submit_query<std::shared_ptr<midipeer_t>>(
      rtpmidid::queue_priority_e::LOW,
      [peer_id](midirouter_t &r) -> std::shared_ptr<midipeer_t> {
        auto it = r.peers_.find(peer_id);
        if (it == r.peers_.end())
          return nullptr;
        return it->second.peer;
      });
}

size_t midirouter_t::peer_count() const {
  return const_cast<midirouter_t *>(this)->submit_query<size_t>(
      rtpmidid::queue_priority_e::LOW,
      [](midirouter_t &r) -> size_t { return r.peers_.size(); });
}

std::vector<peer_id_t> midirouter_t::peer_ids() const {
  return const_cast<midirouter_t *>(this)
      ->submit_query<std::vector<peer_id_t>>(
          rtpmidid::queue_priority_e::LOW,
          [](midirouter_t &r) -> std::vector<peer_id_t> {
            std::vector<peer_id_t> ids;
            ids.reserve(r.peers_.size());
            for (const auto &p : r.peers_)
              ids.push_back(p.first);
            return ids;
          });
}

std::vector<peer_id_t>
midirouter_t::send_targets_for(peer_id_t from) const {
  return const_cast<midirouter_t *>(this)
      ->submit_query<std::vector<peer_id_t>>(
          rtpmidid::queue_priority_e::LOW,
          [from](midirouter_t &r) -> std::vector<peer_id_t> {
            auto it = r.peers_.find(from);
            if (it == r.peers_.end())
              return {};
            return it->second.send_to;
          });
}

std::vector<router_peer_row_t> midirouter_t::status_rows() const {
  return const_cast<midirouter_t *>(this)
      ->submit_query<std::vector<router_peer_row_t>>(
          rtpmidid::queue_priority_e::LOW,
          [](midirouter_t &r) { return r.status_rows_impl(); });
}

void midirouter_t::peer_connection_loop(
    peer_id_t peer_id,
    std::function<void(std::shared_ptr<midipeer_t>)> func) {
  submit_run(rtpmidid::queue_priority_e::LOW,
             [peer_id, func](midirouter_t &r) {
               auto it = r.peers_.find(peer_id);
               if (it == r.peers_.end()) {
                 WARNING("peer_connection_loop: unknown peer {}!", peer_id);
                 return;
               }
               const auto send_to = it->second.send_to;
               for (auto to : send_to) {
                 auto it2 = r.peers_.find(to);
                 if (it2 != r.peers_.end())
                   func(it2->second.peer);
               }
             });
}

// ===========================================================================
// Backwards-compatible enqueue_* aliases
// ===========================================================================

bool midirouter_t::enqueue_send_midi(peer_id_t from, const mididata_t &data) {
  send_midi(from, data);
  return true;
}

bool midirouter_t::enqueue_send_midi(peer_id_t from, peer_id_t to,
                                     const mididata_t &data) {
  send_midi(from, to, data);
  return true;
}

bool midirouter_t::enqueue_connect(peer_id_t from, peer_id_t to) {
  connect(from, to);
  return true;
}

bool midirouter_t::enqueue_disconnect(peer_id_t from, peer_id_t to) {
  disconnect(from, to);
  return true;
}

bool midirouter_t::enqueue_remove_peer(peer_id_t peer_id) {
  remove_peer(peer_id);
  return true;
}

bool midirouter_t::enqueue_event(peer_id_t from, peer_id_t to,
                                 midipeer_event_e evt) {
  event(from, to, evt);
  return true;
}

bool midirouter_t::enqueue_event(peer_id_t from, midipeer_event_e evt) {
  event(from, evt);
  return true;
}

} // namespace rtpmididns

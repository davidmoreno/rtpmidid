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

#include "midipeer.hpp"
#include "dm_json_generated.hpp"
#include "mididata.hpp"
#include "midirouter.hpp"
#include "rtpmidid/shutdown_signals.hpp"
#include "rtpmidid/stats.hpp"
#include <chrono>
#include <variant>

namespace rtpmidid {
midi_packet_t::midi_packet_t(uint32_t from,
                             const rtpmididns::mididata_t &mididata)
    : from_peer_id(from),
      data(mididata.position, mididata.position + mididata.remaining()),
      timestamp_received(std::chrono::steady_clock::now()) {}
} // namespace rtpmidid

namespace rtpmididns {

namespace {

/** Per-thread tag identifying which peer (if any) owns the running thread. */
thread_local midipeer_t *g_current_peer_thread = nullptr;

} // namespace

midipeer_t::~midipeer_t() { stop_thread(); }

std::optional<std::string> midipeer_t::compute_stable_id() const {
  return std::nullopt;
}

bool midipeer_t::on_peer_thread() const {
  return g_current_peer_thread == this;
}

bool midipeer_t::control_peer_command(std::string_view cmd,
                                      std::string_view params_json,
                                      ::rtpmididns::dmjson::writer_t &out,
                                      std::string &out_error) {
  (void)params_json;
  if (cmd == "help") {
    out.begin_array();
    static const char *const cmds[] = {"status"};
    static const char *const desc[] = {"Return peer status"};
    for (std::size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i) {
      out.array_item();
      out.begin_object();
      out.key("name");
      out.string_value(cmds[i]);
      out.key("description");
      out.string_value(desc[i]);
      out.end_object();
    }
    out.end_array();
    return true;
  }
  if (cmd == "status") {
    dmjson::to_json(status(), out);
    return true;
  }
  ERROR("Unknown command: {}", cmd);
  out_error = "Command not implemented";
  return false;
}

void midipeer_t::start_thread() {
  if (thread_running_.exchange(true)) {
    return;
  }
  peer_thread_ = std::thread(&midipeer_t::peer_thread_loop, this);
}

void midipeer_t::stop_thread() {
  if (!thread_running_.load()) {
    return;
  }
  // Message-driven shutdown: handler flips thread_running_ on the peer
  // thread; the loop then exits and drains. Falls back to direct flag
  // flip + wake() only if the queue happens to be full.
  if (!peer_queue_.enqueue(peer_command_t{peer_cmd::shutdown_t{}},
                           rtpmidid::queue_priority_e::NORMAL)) {
    thread_running_.store(false);
    peer_queue_.wake();
  }
  if (peer_thread_.joinable()) {
    peer_thread_.join();
  }
}

void midipeer_t::peer_thread_loop() {
  rtpmidid::block_shutdown_signals();
  g_current_peer_thread = this;

  try {
    peer_command_t cmd;
    while (thread_running_.load()) {
      if (peer_queue_.wait_dequeue(cmd))
        std::visit([this](auto &c) { this->handle(c); }, cmd);
      // wait_dequeue returned false → heartbeat; re-check thread_running_
    }

    // Drain so reply channels never deadlock.
    while (peer_queue_.try_dequeue(cmd))
      std::visit([this](auto &c) { this->handle(c); }, cmd);
  } catch (const std::exception &e) {
    ERROR("peer {}: exception in peer thread loop: {}", peer_id, e.what());
  } catch (...) {
    ERROR("peer {}: unknown exception in peer thread loop", peer_id);
  }

  g_current_peer_thread = nullptr;
}

// ===========================================================================
// Variant dispatch
// ===========================================================================

void midipeer_t::handle(peer_cmd::process_midi_t &cmd) {
  process_midi_packet(cmd.packet);
}

void midipeer_t::handle(peer_cmd::query_internal_latency_stats_t &cmd) {
  rtpmidid::reply_envelope_t env;
  env.id = cmd.reply.id;
  try {
    env.value = std::any(internal_latency_stats_impl());
  } catch (const std::exception &exc) {
    env.error = exc.what();
  } catch (...) {
    env.error = "unknown exception in query_internal_latency_stats";
  }
  if (cmd.reply.channel)
    cmd.reply.channel->post(std::move(env));
}

void midipeer_t::handle(peer_cmd::shutdown_t & /*cmd*/) {
  thread_running_.store(false);
}

// ===========================================================================
// MIDI processing
// ===========================================================================

void midipeer_t::process_midi_packet(const rtpmidid::midi_packet_t &packet) {
  packets_recv++;

  const auto t_before_send = std::chrono::steady_clock::now();
  const auto until_send_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          t_before_send - packet.timestamp_received);
  internal_until_send_stats_.add_stat(until_send_ns);
  internal_last_until_send_ns_.store(until_send_ns.count(),
                                     std::memory_order_relaxed);

  mididata_t mididata(const_cast<uint8_t *>(packet.data.data()),
                      static_cast<uint32_t>(packet.data.size()));

  const auto send_start = std::chrono::steady_clock::now();
  send_midi(packet.from_peer_id, mididata);
  const auto send_end = std::chrono::steady_clock::now();
  const auto send_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      send_end - send_start);
  internal_send_midi_stats_.add_stat(send_ns);
  internal_last_send_midi_ns_.store(send_ns.count(),
                                    std::memory_order_relaxed);

#ifdef RTPMIDID_ENABLE_TIMING
  DEBUG("[TIMING] peer {}: MIDI until send_midi: {:.3f} ms, send_midi: {:.3f} "
        "ms, size: {} bytes",
        peer_id, until_send_ns.count() / 1e6, send_ns.count() / 1e6,
        packet.data.size());
#endif
}

bool midipeer_t::enqueue_midi_packet(const rtpmidid::midi_packet_t &packet) {
  rtpmidid::midi_packet_t stamped = packet;
  stamped.timestamp_received = std::chrono::steady_clock::now();
  peer_cmd::process_midi_t cmd{std::move(stamped)};
  if (!peer_queue_.enqueue(peer_command_t{std::move(cmd)},
                           rtpmidid::queue_priority_e::HIGH)) {
    WARNING_RATE_LIMIT(5, "peer {}: input queue full, dropping MIDI", peer_id);
    return false;
  }
  return true;
}

void midipeer_t::enqueue_to_router(const mididata_t &data) {
  if (!router) {
    WARNING("peer {}: no router, cannot enqueue MIDI", peer_id);
    return;
  }
  router->send_midi(peer_id, data);
}

// ===========================================================================
// Internal latency stats
// ===========================================================================

internal_latency_ms_t midipeer_t::internal_latency_stats_impl() const {
  const auto u = internal_until_send_stats_.average_and_stddev();
  const auto s = internal_send_midi_stats_.average_and_stddev();
  const double last_until_ms =
      internal_last_until_send_ns_.load(std::memory_order_relaxed) / 1e6;
  const double last_send_ms =
      internal_last_send_midi_ns_.load(std::memory_order_relaxed) / 1e6;
  internal_latency_ms_t out = {
      .until_send_midi_ms =
          {
              .last = last_until_ms,
              .average = u.average.count() / 1e6,
              .stddev = u.stddev.count() / 1e6,
          },
      .send_midi_ms =
          {
              .last = last_send_ms,
              .average = s.average.count() / 1e6,
              .stddev = s.stddev.count() / 1e6,
          },
  };
  return out;
}

internal_latency_ms_t midipeer_t::internal_latency_stats() const {
  if (peer_sync_mode() || on_peer_thread()) {
    return internal_latency_stats_impl();
  }
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  const uint64_t id = channel->next_id();
  if (!const_cast<midipeer_t *>(this)->request_internal_latency_stats(channel,
                                                                      id)) {
    return {};
  }
  auto env = channel->wait(id, std::chrono::milliseconds(2000));
  if (!env.error.empty()) {
    return {};
  }
  try {
    return std::any_cast<internal_latency_ms_t>(env.value);
  } catch (const std::bad_any_cast &) {
    return {};
  }
}

bool midipeer_t::request_internal_latency_stats(
    std::shared_ptr<rtpmidid::reply_channel_t> channel, uint64_t id) {
  if (!channel)
    return false;
  if (peer_sync_mode() || on_peer_thread()) {
    rtpmidid::reply_envelope_t env;
    env.id = id;
    try {
      env.value = std::any(internal_latency_stats_impl());
    } catch (const std::exception &exc) {
      env.error = exc.what();
    } catch (...) {
      env.error = "unknown exception in request_internal_latency_stats";
    }
    channel->post(std::move(env));
    return true;
  }
  peer_cmd::query_internal_latency_stats_t cmd;
  cmd.reply = rtpmidid::reply_slot_t{std::move(channel), id};
  if (!peer_queue_.enqueue(peer_command_t{std::move(cmd)},
                           rtpmidid::queue_priority_e::LOW)) {
    return false;
  }
  return true;
}

} // namespace rtpmididns

const char *format_as(rtpmididns::midipeer_event_e event) {
  switch (event) {
  case rtpmididns::midipeer_event_e::CONNECTED_ROUTER:
    return "CONNECTED_ROUTER";
  case rtpmididns::midipeer_event_e::DISCONNECTED_ROUTER:
    return "DISCONNECTED_ROUTER";
  case rtpmididns::midipeer_event_e::CONNECTED_PEER:
    return "CONNECTED_PEER";
  case rtpmididns::midipeer_event_e::DISCONNECTED_PEER:
    return "DISCONNECTED_PEER";
  default:
    return "UNKNOWN";
  }
}

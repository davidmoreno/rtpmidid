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
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rtpmididns {

/**
 * @short Per-WebSocket-connection event subscription manager.
 *
 * One instance per WS connection. Tracks which event channels the client
 * subscribed to, and provides a thread-safe `emit()` that forwards
 * pre-serialised JSON events to the WebSocket.
 *
 * Thread safety: `subscribe`/`unsubscribe` called from the WS handler
 * thread; `emit()` called from router/poller threads via signals.
 * Protected by an internal mutex. `httplib::WebSocket::send()` is also
 * internally mutex-protected, so we can call it from any thread.
 */
class event_subscription_manager_t {
public:
  /** Callback that sends a pre-serialised JSON string to the WS client. */
  using send_fn = std::function<void(const std::string &)>;

  event_subscription_manager_t() = default;
  ~event_subscription_manager_t() = default;

  event_subscription_manager_t(const event_subscription_manager_t &) = delete;
  event_subscription_manager_t &
  operator=(const event_subscription_manager_t &) = delete;
  event_subscription_manager_t(event_subscription_manager_t &&) = delete;
  event_subscription_manager_t &
  operator=(event_subscription_manager_t &&) = delete;

  /** Set the send callback (called once after WS upgrade). */
  void set_send_fn(send_fn fn);

  /** Subscribe to one or more event channels. */
  void subscribe(const std::vector<std::string> &channels);

  /** Unsubscribe from one or more event channels. */
  void unsubscribe(const std::vector<std::string> &channels);

  /** Remove all subscriptions. */
  void unsubscribe_all();

  /** Thread-safe: returns true if channel is subscribed. */
  bool is_subscribed(std::string_view channel) const;

  /**
   * Thread-safe: emit an event if the channel is subscribed.
   *
   * Serialises the event as:
   *   {"event":"<channel>","params":<params_json>}
   *
   * @param channel  Event channel name (e.g. "router.peer_added").
   * @param params_json  Pre-serialised JSON params payload.
   */
  void emit(std::string_view channel, const std::string &params_json);

private:
  /** Serialise the event envelope and push to the send callback. */
  void emit_locked(std::string_view channel, const std::string &params_json);

  mutable std::mutex mutex_;
  std::set<std::string> channels_;
  send_fn send_;
};

} // namespace rtpmididns

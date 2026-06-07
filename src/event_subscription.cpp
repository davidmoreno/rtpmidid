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
#include "event_subscription.hpp"
#include <algorithm>
#include <rtpmidid/logger.hpp>

namespace rtpmididns {

void event_subscription_manager_t::set_send_fn(send_fn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  send_ = std::move(fn);
}

void event_subscription_manager_t::subscribe(
    const std::vector<std::string> &channels) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &ch : channels) {
    if (!ch.empty())
      channels_.insert(ch);
  }
  DEBUG("event_subscription: subscribed {} channels (total {})",
        channels.size(), channels_.size());
}

void event_subscription_manager_t::unsubscribe(
    const std::vector<std::string> &channels) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &ch : channels) {
    channels_.erase(ch);
  }
  DEBUG("event_subscription: unsubscribed {} channels (total {})",
        channels.size(), channels_.size());
}

void event_subscription_manager_t::unsubscribe_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  channels_.clear();
}

bool event_subscription_manager_t::is_subscribed(
    std::string_view channel) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return channels_.find(std::string(channel)) != channels_.end();
}

void event_subscription_manager_t::emit(std::string_view channel,
                                        const std::string &params_json) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (channels_.find(std::string(channel)) == channels_.end())
    return;
  emit_locked(channel, params_json);
}

void event_subscription_manager_t::emit_locked(
    std::string_view channel, const std::string &params_json) {
  if (!send_)
    return;
  // Serialise as {"event":"<channel>","params":<params_json>}
  std::string json;
  json.reserve(channel.size() + params_json.size() + 32);
  json += R"({"event":")";
  json += channel;
  json += R"(","params":)";
  json += params_json;
  json += '}';
  send_(json);
}

} // namespace rtpmididns

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

/// Worker and DNS messages (design D10/D13): blocking jobs and typed DNS
/// results. Lives next to `worker_actor.hpp`.

#pragma once

#include "mailbox.hpp"
#include "message_core.hpp"
#include <functional>
#include <string>
#include <vector>

namespace rtpmididns {

/// A blocking job for the worker actor (C++23 `std::move_only_function`;
/// small closures stored inline; the worker never knows result types).
struct worker_job_t {
  std::move_only_function<void()> job;
};
/// Typed DNS resolution result posted back to the requester.
struct dns_resolved_t {
  hdr_t hdr;
  std::string hostname;
  std::string port;
  std::vector<std::string> addresses; // empty = resolution failed
};

// One-line rendering for mailbox drop diagnostics (see message_core.hpp).
inline std::string to_string(const dns_resolved_t &m) {
  return "dns_resolved{hostname=\"" + m.hostname + "\", port=\"" + m.port +
         "\", addresses=" + std::to_string(m.addresses.size()) + "}";
}

/// The worker's accepted control messages: jobs only.
using worker_control_t = std::variant<stop_t, worker_job_t>;
/// The worker's mailbox.
using worker_mailbox_t = mailbox_t<std::monostate, worker_control_t>;

} // namespace rtpmididns

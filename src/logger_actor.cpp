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

#include "logger_actor.hpp"
#include <iostream>

namespace rtpmididns {

// The mailbox the global log sink posts to. Kept in a shared_ptr (not a
// raw handle) so posting to the logger after it has been stopped stays
// safe: late messages are bounded and discarded with the mailbox, never
// dangling.
static std::shared_ptr<logger_mailbox_t> logger_mailbox_global;

/// Installed into rtpmidid::logger_log_sink by logger_actor_t::install().
static void route_log(rtpmidid::log_message_t msg) {
  if (auto mailbox = logger_mailbox_global) {
    mailbox->post_data(std::move(msg));
  }
}

logger_actor_t::logger_actor_t(actor_config_t config)
    : actor_t(std::move(config)) {}

void logger_actor_t::install() {
  logger_mailbox_global = mailbox();
  rtpmidid::logger_log_sink = route_log;
}

void logger_actor_t::on_data(rtpmidid::log_message_t &&msg) {
  std::cout << rtpmidid::logger_format_line(msg) << std::endl;
}

} // namespace rtpmididns

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

/// Logger actor: owns the daemon's stdout logging. The INFO/WARNING/DEBUG/
/// ERROR macros keep their exact call syntax; the producing thread now
/// filters by level, formats the message body, and posts a `log_message_t`
/// (level, origin, already-formatted text) here through the global sink
/// installed by `install()`. This actor — a single thread — renders and
/// writes the line, so output is serialized and never interleaves between
/// threads. Because the stop control message is processed only after the
/// data lane is drained (data-first policy), requesting stop flushes every
/// queued line with no explicit drain logic.

#pragma once

#include "actor.hpp"
#include "rtpmidid/logger.hpp"

namespace rtpmididns {

/// The logger actor's accepted control messages: the universal stop only.
using logger_control_t = std::variant<stop_t>;
/// The logger actor's mailbox: formatted log messages on the data lane,
/// lifecycle control on the control lane.
using logger_mailbox_t = mailbox_t<rtpmidid::log_message_t, logger_control_t>;

class logger_actor_t
    : public actor_t<rtpmidid::log_message_t, logger_control_t> {
public:
  using control_messages = logger_control_t;
  explicit logger_actor_t(actor_config_t config);

  /// Route the logging macros to this actor. Call once after construction
  /// (before or after `start()`); producers that cannot reach an actor
  /// (lib-only programs, tests, early startup) keep the direct-print
  /// fallback.
  void install();

protected:
  void on_data(rtpmidid::log_message_t &&msg) override;
};

} // namespace rtpmididns

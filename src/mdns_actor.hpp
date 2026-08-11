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

/// Dedicated mdns actor (design D13; task 6.3): owns the avahi fds in its
/// own poller at normal scheduling priority, so mdns neither contends with
/// the data plane nor stalls control. Serves status and announcement
/// mutations via mailbox request/response to control connection actors.

#pragma once

#include "actor.hpp"
#include "messages.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"
#include <memory>

namespace rtpmididns {

class mdns_actor_t : public actor_t {
public:
  explicit mdns_actor_t(
      actor_config_t config = actor_config_t{.name = "mdns"});

  /// The mdns object (for tests that need the legacy global-free access).
  rtpmidid::mdns_rtpmidi_t *mdns() const { return mdns_.get(); }

protected:
  void on_start() override;
  void on_control(control_message_t &&msg) override;

private:
  std::unique_ptr<rtpmidid::mdns_rtpmidi_t> mdns_;
};

} // namespace rtpmididns

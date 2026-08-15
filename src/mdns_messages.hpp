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

/// mdns actor messages (design D13; task 6.3): status/announcement
/// commands plus the replies to its create-port/spawn requests. Lives next
/// to `mdns_actor.hpp`.

#pragma once

#include "alsa_messages.hpp"
#include "mailbox.hpp"
#include "message_core.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace rtpmididns {

struct mdns_announcement_info_t {
  std::string name;
  int32_t port = 0;
};
struct mdns_remote_info_t {
  std::string name;
  std::string address;
  int32_t port = 0;
};
struct mdns_status_req_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
struct mdns_status_resp_t {
  hdr_t hdr;
  bool available = false;
  std::vector<mdns_announcement_info_t> announcements;
  std::vector<mdns_remote_info_t> remote_announcements;
};
/// Announce/unannounce/remove an rtpmidi service (control-socket users).
struct mdns_announce_t {
  std::string name;
  int32_t port = 0;
};
struct mdns_unannounce_t {
  std::string name;
  int32_t port = 0;
};
struct mdns_remove_t {
  std::string name;
  std::string hostname;
  int32_t port = 0;
};

/// The mdns actor's accepted control messages. No MIDI, no routing: status
/// and announcement commands plus the replies to its waiting-port creation
/// requests (the session side is the rtpmidi server's job).
using mdns_control_t =
    std::variant<stop_t, mdns_status_req_t, mdns_announce_t, mdns_unannounce_t,
                 mdns_remove_t, alsa_port_result_t>;
/// The mdns actor's mailbox.
using mdns_mailbox_t = mailbox_t<std::monostate, mdns_control_t>;

} // namespace rtpmididns

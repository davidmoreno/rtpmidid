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
#pragma once

#include "mailbox.hpp"
#include "mdns_messages.hpp"
#include "peer_messages.hpp"
#include "router_messages.hpp"
#include "worker_messages.hpp"
#include <chrono>
#include <functional>
#include <vector>

#include <rtpmidid/iobytes.hpp>

namespace rtpmididns {
/// Permissive control variant for tests: every message the test actors
/// post or the test reply mailboxes receive. Each test could declare a
/// narrower set; a shared one keeps the test files small.
using test_control_t =
    std::variant<stop_t, stopped_t, actor_died_t, reap_actor_t, peer_event_t,
                 control_payload_t, registered_t, peer_status_req_t,
                 peer_command_t, peer_status_resp_t, peer_command_resp_t,
                 ack_t, peer_ids_result_t, status_head_t, mdns_status_resp_t,
                 dns_resolved_t>;
/// Permissive test mailbox (accepts the test control set + the data lane).
using test_mailbox_t = mailbox_t<data_message_t, test_control_t>;
} // namespace rtpmididns

class test_client_t {
public:
  int sockfd;
  int local_port;
  int remote_port;
  // UDP connection to this "localhost":port
  test_client_t(int local_port, int remote_port);
  void send(rtpmidid::io_bytes_reader &&data);
  void recv(rtpmidid::io_bytes_reader &&data);
};

rtpmidid::io_bytes_managed hex_to_bin(const std::string &str);
void poller_wait_for(std::chrono::milliseconds ms);
void poller_wait_until(
    const std::function<bool(void)> &f,
    std::chrono::milliseconds ms = std::chrono::milliseconds(500));

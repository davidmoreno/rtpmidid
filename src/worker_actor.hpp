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

/// Worker actor (design D10/D13; tasks 6.1, 6.2): runs blocking jobs
/// (DNS resolution, ...) as `std::move_only_function` messages on a FIFO
/// control lane at idle scheduling priority, so it never takes CPU from
/// the data plane. Closures capture the requester's mailbox and corr and
/// post typed results back; the worker never knows result types.

#pragma once

#include "actor.hpp"
#include "messages.hpp"
#include <arpa/inet.h>
#include <functional>
#include <netdb.h>
#include <string>
#include <sys/socket.h>

namespace rtpmididns {

class worker_actor_t : public actor_t {
public:
  explicit worker_actor_t(
      actor_config_t config = actor_config_t{.name = "worker",
                                             .scheduling =
                                                 scheduling_class_t::idle});

  /// Post a blocking job; jobs execute FIFO on the worker thread.
  void enqueue(std::move_only_function<void()> job) {
    mailbox()->post_control(worker_job_t{std::move(job)});
  }

protected:
  void on_control(control_message_t &&msg) override;
};

/**
 * Resolve `hostname:port` on the worker; posts `dns_resolved{hdr, ...}`
 * with the numeric addresses back to `reply_to` (empty on failure).
 * Blocking getaddrinfo never touches an actor message loop (6.2).
 */
inline void resolve_dns(worker_actor_t &worker, const std::string &hostname,
                        const std::string &port, mailbox_handle_t reply_to,
                        uint64_t corr) {
  worker.enqueue([hostname, port, reply_to = std::move(reply_to), corr] {
    std::vector<std::string> addresses;
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = nullptr;
    if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &res) == 0) {
      for (auto *ai = res; ai != nullptr; ai = ai->ai_next) {
        char buf[INET6_ADDRSTRLEN] = {};
        if (getnameinfo(ai->ai_addr, ai->ai_addrlen, buf, sizeof(buf),
                        nullptr, 0, NI_NUMERICHOST) == 0) {
          addresses.emplace_back(buf);
        }
      }
      freeaddrinfo(res);
    }
    if (reply_to) {
      reply_to->post_control(
          dns_resolved_t{hdr_t{corr}, hostname, port, std::move(addresses)});
    }
  });
}

} // namespace rtpmididns

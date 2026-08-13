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

#include "peer_actor.hpp"
#include "peer_status_jsondm.hpp"
#include "rtpmidid/logger.hpp"

namespace rtpmididns {

peer_actor_t::peer_actor_t(actor_config_t config)
    : actor_t(std::move(config)) {}

void peer_actor_t::on_start() {
  // Registered gate (5.1): park until the router posts `registered{ids}`.
  // The data lane keeps flowing; wire traffic arriving before the gate is
  // dropped by on_data. On timeout the peer self-terminates.
  wait_for(
      [](const peer_control_t &m) {
        return std::holds_alternative<registered_t>(m);
      },
      registered_timeout_,
      [this](std::optional<peer_control_t> res) {
        if (res) {
          registered_ = true;
          registered_ids_ = std::get<registered_t>(*res).ids;
          INFO("Peer {}: registered with {} id(s).", name(),
               registered_ids_.size());
        } else {
          WARNING("Peer {}: did not receive `registered` within {} ms; "
                  "self-terminating.",
                  name(), registered_timeout_.count());
          request_stop_token();
        }
      });
}

void peer_actor_t::on_data(data_message_t &&msg) {
  if (msg.kind != data_message_t::kind_t::midi_to_wire) {
    WARNING("Peer {}: unexpected data message kind; dropped.", name());
    return;
  }
  if (!registered_) {
    WARNING_RATE_LIMIT(5, "Peer {}: midi_to_wire before `registered`; "
                          "dropped.",
                       name());
    return;
  }
  send_to_wire(msg.to, msg.from, std::move(msg.payload));
}

void peer_actor_t::on_control(peer_control_t &&msg) {
  std::visit(
      [this](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, peer_status_req_t>) {
          peer_status_variant_t s;
          try {
            s = status();
          } catch (const std::exception &e) {
            s = peer_error_t{e.what()};
          }
          m.reply_to.post_control(
              peer_status_resp_t{m.hdr, m.target, std::move(s)});
        } else if constexpr (std::is_same_v<T, peer_command_t>) {
          bool is_error = false;
          std::string result;
          try {
            result = command_impl(m.cmd, m.params_json);
          } catch (const std::exception &e) {
            result = FMT::format(R"({{"error": "{}"}})", e.what());
            is_error = true;
          }
          m.reply_to.post_control(peer_command_resp_t{
              m.hdr, m.peer_id, std::move(result), is_error});
        } else if constexpr (std::is_same_v<T, registered_t>) {
          registered_ = true;
          registered_ids_ = m.ids;
        } else if constexpr (std::is_same_v<T, peer_event_t>) {
          on_peer_event(m);
        }
        // dns_resolved_t / udp_datagram_t are handled by the network peer.
      },
      msg);
}

std::string peer_actor_t::command_impl(const std::string &cmd,
                                       std::string_view) {
  if (cmd == "help") {
    return "{}";
  }
  if (cmd == "status") {
    std::string out;
    jsondm::serialize(status(), out);
    return out;
  }
  ERROR("Peer {}: unknown command: {}", name(), cmd);
  throw std::runtime_error("Command not implemented");
}

} // namespace rtpmididns

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

#include "control_socket_actor.hpp"
#include "control_commands_jsondm.hpp"
#include "control_status_jsondm.hpp"
#include "local_rawmidi_peer_actor.hpp"
#include "network_rtpmidi_peer_actor.hpp"
#include "peer_status_jsondm.hpp"
#include "router_actor.hpp"
#include "rtpmidid/logger.hpp"
#include "settings.hpp"
#include "stringpp.hpp"
#include <algorithm>
#include <fcntl.h>
#include <regex>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace rtpmididns {

static const std::regex PEER_COMMAND_RE("^(\\d*)\\.(.*)");
static constexpr const char *MSG_TOO_LONG =
    "{\"event\": \"close\", \"detail\": \"Message too long\", \"code\": 1}\n";

// ---------------------------------------------------------------------------
// Response composition (byte-compatible with the legacy control socket)
// ---------------------------------------------------------------------------

static std::string compose_response(const std::optional<std::string> &id,
                                    const char *kind,
                                    std::string_view payload) {
  std::string out;
  jsondm::Writer w(out);
  w.obj_begin();
  {
    jsondm::Writer::member_guard g(w, "id");
    jsondm::write(w, id);
  }
  {
    jsondm::Writer::member_guard g(w, kind);
    w.append(payload);
  }
  w.obj_end();
  w.flush();
  return out;
}

// Assigns the router-owned members to a peer status (not peer_error_t).
struct peer_status_set_common_t {
  peer_id_t id;
  std::vector<peer_id_t> send_to;
  peer_stats_t stats;
  std::string type;
  void operator()(peer_error_t &) const {}
  template <class T> void operator()(T &s) const {
    s.id = id;
    s.send_to = send_to;
    s.stats = stats;
    s.type = type;
  }
};

static std::string port_to_string(const std::variant<std::string, int> &p) {
  return std::visit(
      [](const auto &v) -> std::string {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int>) {
          return std::to_string(v);
        } else {
          return v;
        }
      },
      p);
}

// ---------------------------------------------------------------------------
// Connection actor
// ---------------------------------------------------------------------------

control_connection_actor_t::control_connection_actor_t(
    actor_config_t config, int client_fd, mailbox_handle_t router_mailbox,
    mailbox_handle_t mdns_mailbox, std::shared_ptr<worker_actor_t> worker,
    std::string version, std::chrono::milliseconds request_deadline)
    : actor_t(std::move(config)), client_fd_(client_fd),
      router_mailbox_(std::move(router_mailbox)),
      mdns_mailbox_(std::move(mdns_mailbox)), worker_(std::move(worker)),
      version_(std::move(version)), request_deadline_(request_deadline) {}

void control_connection_actor_t::on_start() {
  if (client_fd_ >= 0) {

    client_listener_ =
        add_fd_in(client_fd_, [this](int) { on_client_data(); });
  }
}

void control_connection_actor_t::on_stop() {
  if (client_fd_ >= 0) {
    ::close(client_fd_);
    client_fd_ = -1;
  }
}

void control_connection_actor_t::on_client_data() {

  char buf[1024];
  for (;;) {
    const ssize_t n = ::recv(client_fd_, buf, sizeof(buf), MSG_DONTWAIT);
    if (n <= 0) {
      // Client disconnected (or error): stop; abandoned waits are dropped
      // with the mailbox (7.3).
      if (n == 0 || errno != EAGAIN) {
        request_stop_token();
      }
      break;
    }
    if (n >= (ssize_t)sizeof(buf) - 1) {
      ::write(client_fd_, MSG_TOO_LONG, strlen(MSG_TOO_LONG));
      request_stop_token();
      return;
    }
    read_buffer_.append(buf, n);
  }
  // Process complete lines (the legacy socket processes one command per
  // readable event; newline handling mirrors the cli).
  while (!read_buffer_.empty()) {
    const auto nl = read_buffer_.find('\n');
    const auto end = (nl == std::string::npos) ? read_buffer_.size() : nl;
    if (nl == std::string::npos && end == read_buffer_.size()) {
      break; // incomplete line
    }
    std::string line = read_buffer_.substr(0, end);
    read_buffer_.erase(0, end + (nl == std::string::npos ? 0 : 1));
    if (!line.empty()) {
      parse_and_dispatch(line);
    }
  }
}

void control_connection_actor_t::parse_and_dispatch(const std::string &command) {
  std::string method;
  std::optional<std::string> id;
  std::string_view params_span;
  try {
    {
      jsondm::Reader pre(command);
      pre.obj_begin();
      while (pre.next_key()) {
        if (pre.key() == "method") {
          jsondm::read(pre, method);
          break;
        }
        pre.skip_value();
      }
    }
    {
      jsondm::Reader r(command);
      r.obj_begin();
      while (r.next_key()) {
        auto key = r.key();
        if (key == "method") {
          r.skip_value();
        } else if (key == "id") {
          jsondm::read(r, id);
        } else if (key == "params") {
          params_span = r.skip_value_span();
        } else {
          r.skip_value();
        }
      }
      r.obj_end();
    }
  } catch (const std::exception &e) {
    // Byte-compatible with the legacy parse error: the raw error object.
    std::string out;
    jsondm::serialize(command_error_t{e.what()}, out);
    out += "\n";
    ::write(client_fd_, out.data(), out.size());
    return;
  }

  pending_command_t cmd{std::move(id), method};
  try {
    if (method == "status") {
      cmd_status(cmd);
    } else if (method == "router.connect") {
      cmd_router_connect(cmd, params_span);
    } else if (method == "router.disconnect") {
      cmd_router_disconnect(cmd, params_span);
    } else if (method == "router.remove") {
      cmd_router_remove(cmd, params_span);
    } else if (method == "connect") {
      cmd_connect(cmd, params_span);
    } else if (method == "router.create") {
      cmd_router_create(cmd, params_span);
    } else if (method == "mdns.remove") {
      cmd_mdns_remove(cmd, params_span);
    } else if (method == "export.rawmidi") {
      cmd_export_rawmidi(cmd, params_span);
    } else if (method == "help") {
      cmd_help(cmd);
    } else {
      std::smatch match;
      if (std::regex_match(method, match, PEER_COMMAND_RE)) {
        cmd_peer_command(cmd, match[1], match[2], params_span);
      } else {
        respond_error(cmd, FMT::format("Unknown method '{}'", method));
      }
    }
  } catch (const std::exception &e) {
    respond_error(cmd, e.what());
  }
}

void control_connection_actor_t::respond(const pending_command_t &cmd,
                                         const char *kind,
                                         std::string_view payload) {
  if (gone_) {
    return;
  }
  std::string out = compose_response(cmd.id, kind, payload);
  out += "\n";
  [[maybe_unused]] const ssize_t w =
      ::write(client_fd_, out.data(), out.size());
}

void control_connection_actor_t::respond_error(const pending_command_t &cmd,
                                               const std::string &msg) {
  respond(cmd, "error", msg);
}

void control_connection_actor_t::on_control(control_message_t &&msg) {
  std::visit(
      [this](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, ack_t>) {
          // An awaited ack resolved; the waiter continuation handles it.
        } else if constexpr (std::is_same_v<T, peer_ids_result_t>) {
        } else if constexpr (std::is_same_v<T, peer_command_resp_t>) {
        } else if constexpr (std::is_same_v<T, mdns_status_resp_t>) {
          if (gather_.corr != 0 && m.hdr.corr == gather_.corr) {
            gather_.mdns = m;
            gather_.mdns_done = true;
          }
        }
      },
      msg.v);
}

// --- waits ------------------------------------------------------------------

void control_connection_actor_t::wait_ack(const pending_command_t &cmd,
                                          uint64_t corr,
                                          const char *success_payload) {
  wait_for(
      [corr](const control_message_t &m) {
        return std::holds_alternative<ack_t>(m.v) &&
               std::get<ack_t>(m.v).hdr.corr == corr;
      },
      request_deadline_,
      [this, cmd, corr, success_payload](std::optional<control_message_t> res) {
        if (res) {
          auto &a = std::get<ack_t>(res->v);
          if (a.ok) {
            respond(cmd, "result", success_payload);
          } else {
            respond_error(cmd, a.error.empty() ? "failed" : a.error);
          }
        } else {
          respond_error(cmd, "deadline: no response from router/peer");
        }
        (void)corr;
      });
}

void control_connection_actor_t::spawn_via_router(const pending_command_t &cmd,
                                                  uint64_t corr,
                                                  spawn_peer_t &&sp) {
  sp.hdr = hdr_t{corr};
  sp.reply_to = mailbox();
  router_mailbox_->post_control(std::move(sp));
  wait_for(
      [corr](const control_message_t &m) {
        return std::holds_alternative<peer_ids_result_t>(m.v) &&
               std::get<peer_ids_result_t>(m.v).hdr.corr == corr;
      },
      request_deadline_,
      [this, cmd](std::optional<control_message_t> res) {
        if (res) {
          auto &r = std::get<peer_ids_result_t>(res->v);
          std::string out;
          jsondm::serialize(std::vector<std::string>{"ok"}, out);
          respond(cmd, "result", out);
          (void)r;
        } else {
          respond_error(cmd, "deadline: router did not spawn the peer");
        }
      });
}

// --- commands ---------------------------------------------------------------

void control_connection_actor_t::cmd_status(const pending_command_t &cmd) {
  gather_.corr = next_corr();
  gather_.responses.clear();
  gather_.expected.clear();
  gather_.mdns_done = false;
  gather_.finished = false;
  gather_status(gather_.corr);
  pending_cmd_ = cmd;
}

void control_connection_actor_t::gather_status(uint64_t corr) {
  // The request is posted exactly once per gather; the waiter re-parks
  // without re-requesting.
  router_mailbox_->post_control(status_req_t{hdr_t{corr}, mailbox()});
  re_park_gather(corr);
}

void control_connection_actor_t::re_park_gather(uint64_t corr) {
  wait_for(
      [this, corr](const control_message_t &m) {
        if (auto *h = std::get_if<status_head_t>(&m.v)) {
          return h->hdr.corr == corr;
        }
        if (auto *r = std::get_if<peer_status_resp_t>(&m.v)) {
          return r->hdr.corr == corr;
        }
        if (auto *ev = std::get_if<peer_event_t>(&m.v)) {
          // Only relevant events (an expected peer went away) wake us.
          return gather_.expected.count(ev->peer_id) != 0;
        }
        return false;
      },
      request_deadline_,
      [this, corr](std::optional<control_message_t> res) {
        on_gather_step(corr, std::move(res));
      });
}

void control_connection_actor_t::on_gather_step(
    uint64_t corr, std::optional<control_message_t> res) {
  if (!res) {
    finish_gather(pending_cmd_, corr);
    return;
  }
  if (auto *h = std::get_if<status_head_t>(&res->v)) {
    gather_.metas = h->peers;
    for (auto &m : h->peers) {
      gather_.expected.insert(m.id);
    }
  } else if (auto *r = std::get_if<peer_status_resp_t>(&res->v)) {
    gather_.responses[r->peer_id] = r->status;
    gather_.expected.erase(r->peer_id);
  } else if (auto *ev = std::get_if<peer_event_t>(&res->v)) {
    // A peer event for an expected peer: unreachable, the set shrinks.
    gather_.expected.erase(ev->peer_id);
  }
  if (gather_.expected.empty()) {
    finish_gather(pending_cmd_, corr);
    return;
  }
  // Keep waiting for the remaining expected peers (no re-request).
  re_park_gather(corr);
}

void control_connection_actor_t::finish_gather(const pending_command_t &cmd,
                                               uint64_t corr) {
  if (gather_.finished) {
    return;
  }
  gather_.finished = true;
  auto finish = [this, cmd]() {
    daemon_status_t st;
    st.version = version_;
    st.settings.alsa_name = settings.alsa_name;
    st.settings.control_filename = settings.control;
    // Router array: typed status merged with the router-assigned members.
    for (auto &meta : gather_.metas) {
      auto it = gather_.responses.find(meta.id);
      if (it != gather_.responses.end()) {
        auto status = it->second;
        std::visit(
            peer_status_set_common_t{meta.id, meta.send_to, meta.stats,
                                     meta.type},
            status);
        st.router.push_back(std::move(status));
      } else {
        st.router.push_back(peer_error_t{"unresponsive"});
      }
    }
    mdns_status_t m;
    if (gather_.mdns_done && gather_.mdns.available) {
      m.status = "Available";
      for (auto &a : gather_.mdns.announcements) {
        m.announcements.push_back({a.name, a.port});
      }
      for (auto &r : gather_.mdns.remote_announcements) {
        m.remote_announcements.push_back({r.name, r.address, r.port});
      }
    } else {
      m.status = "Not available";
    }
    st.mdns = m;
    std::string out;
    jsondm::serialize(st, out);
    respond(cmd, "result", out);
  };
  if (mdns_mailbox_ && !gather_.mdns_done) {
    const auto mdns_corr = next_corr();
    mdns_mailbox_->post_control(mdns_status_req_t{hdr_t{mdns_corr}, mailbox()});
    wait_for(
        [mdns_corr](const control_message_t &m) {
          return std::holds_alternative<mdns_status_resp_t>(m.v) &&
                 std::get<mdns_status_resp_t>(m.v).hdr.corr == mdns_corr;
        },
        request_deadline_, [finish = std::move(finish)](
                                std::optional<control_message_t>) { finish(); });
    return;
  }
  finish();
  (void)corr;
}

void control_connection_actor_t::cmd_router_connect(
    const pending_command_t &cmd, std::string_view params) {
  router_connect_params_t p;
  jsondm::deserialize(params, p);
  const auto corr = next_corr();
  router_mailbox_->post_control(
      connect_t{hdr_t{corr}, mailbox(), p.from, p.to});
  wait_ack(cmd, corr);
}

void control_connection_actor_t::cmd_router_disconnect(
    const pending_command_t &cmd, std::string_view params) {
  router_connect_params_t p;
  jsondm::deserialize(params, p);
  const auto corr = next_corr();
  router_mailbox_->post_control(
      disconnect_t{hdr_t{corr}, mailbox(), p.from, p.to});
  wait_ack(cmd, corr);
}

void control_connection_actor_t::cmd_router_remove(
    const pending_command_t &cmd, std::string_view params) {
  remove_params_t p;
  jsondm::deserialize(params, p);
  const auto corr = next_corr();
  router_mailbox_->post_control(remove_peer_t{hdr_t{corr}, mailbox(), p.peer_id});
  wait_ack(cmd, corr);
}

void control_connection_actor_t::cmd_connect(const pending_command_t &cmd,
                                             std::string_view params) {
  // Legacy accepts: [hostname] | [hostname,port] | [name,hostname,port] |
  // {name,hostname,port}; name defaults to hostname, port defaults 5004.
  std::string name, hostname, port;
  bool error = false;
  try {
    jsondm::Reader r(params);
    auto t = r.peek_type();
    if (t == jsondm::Reader::type::array) {
      std::vector<std::string> parts;
      r.arr_begin();
      while (r.next_elem()) {
        std::string s;
        jsondm::read(r, s);
        parts.push_back(s);
      }
      r.arr_end();
      switch (parts.size()) {
      case 1: name = hostname = parts[0]; port = "5004"; break;
      case 2: name = hostname = parts[0]; port = parts[1]; break;
      case 3: name = parts[0]; hostname = parts[1]; port = parts[2]; break;
      default: error = true;
      }
    } else if (t == jsondm::Reader::type::object) {
      connect_params_t p;
      jsondm::deserialize(params, p);
      name = p.name;
      hostname = p.hostname;
      port = p.port;
      if (name.empty() || hostname.empty() || port.empty())
        error = true;
    } else {
      error = true;
    }
  } catch (const jsondm::exception &) {
    error = true;
  }
  if (error) {
    std::string out;
    jsondm::serialize(
        command_error_t{"Need 1 param (hostname:hostname:5004), 2 params "
                        "(hostname:port), 3 params (name,hostname,port) or "
                        "a dict{name, hostname, port}"},
        out);
    respond_error(cmd, out);
    return;
  }
  // DNS on the worker, then spawn the client peer via the router.
  const auto corr = next_corr();
  const auto dns_corr = next_corr();
  if (!worker_) {
    respond_error(cmd, "no worker for DNS");
    return;
  }
  resolve_dns(*worker_, hostname, port, mailbox(), dns_corr);
  wait_for(
      [dns_corr](const control_message_t &m) {
        return std::holds_alternative<dns_resolved_t>(m.v) &&
               std::get<dns_resolved_t>(m.v).hdr.corr == dns_corr;
      },
      request_deadline_,
      [this, cmd, corr, name, hostname, port,
       worker = worker_](std::optional<control_message_t> res) {
        if (!res) {
          respond_error(cmd, "deadline: DNS resolution");
          return;
        }
        auto &dns = std::get<dns_resolved_t>(res->v);
        if (dns.addresses.empty()) {
          respond_error(cmd, "could not resolve " + hostname);
          return;
        }
        spawn_peer_t sp;
        sp.type = "network_rtpmidi_peer_t";
        sp.meta = name;
        sp.factory =
            [worker, name, hostname, port](
                const mailbox_handle_t &sup, peer_id_t pid) {
              return std::make_shared<network_rtpmidi_peer_actor_t>(
                  actor_config_t{.name = name,
                                 .id = pid,
                                 .supervisor_mailbox = sup},
                  hostname, port, "0", worker);
            };
        spawn_via_router(cmd, corr, std::move(sp));
      });
}

void control_connection_actor_t::cmd_router_create(
    const pending_command_t &cmd, std::string_view params) {
  create_params_t p;
  jsondm::deserialize(params, p);
  if (p.type == "local_rawmidi_t") {
    if (!p.name || !p.device) {
      throw jsondm::exception("router.create: local_rawmidi_t needs name and device");
    }
    const auto corr = next_corr();
    spawn_peer_t sp;
    sp.type = "local_rawmidi_peer_t";
    sp.meta = *p.name;
    sp.factory = [name = *p.name, device = *p.device](
                     const mailbox_handle_t &sup, peer_id_t pid) {
      const int fd = ::open(device.c_str(), O_RDWR | O_NONBLOCK);
      if (fd < 0) {
        throw std::runtime_error("cannot open " + device);
      }
      return std::make_shared<local_rawmidi_peer_actor_t>(
          actor_config_t{.name = name, .id = pid,
                         .supervisor_mailbox = sup},
          device, name, fd);
    };
    spawn_via_router(cmd, corr, std::move(sp));
  } else if (p.type == "network_rtpmidi_client_t") {
    if (!p.name || !p.hostname || !p.port) {
      throw jsondm::exception("router.create: network_rtpmidi_client_t needs "
                              "name, hostname and port");
    }
    const auto corr = next_corr();
    spawn_peer_t sp;
    sp.type = "network_rtpmidi_peer_t";
    sp.meta = *p.name;
    sp.factory = [worker = worker_, name = *p.name, hostname = *p.hostname,
                  port = port_to_string(*p.port)](const mailbox_handle_t &sup,
                                                  peer_id_t pid) {
      return std::make_shared<network_rtpmidi_peer_actor_t>(
          actor_config_t{.name = name, .id = pid,
                         .supervisor_mailbox = sup},
          hostname, port, "0", worker);
    };
    spawn_via_router(cmd, corr, std::move(sp));
  } else if (p.type == "list") {
    router_create_help_t help;
    help.local_rawmidi_t =
        router_create_help_entry_t{"Name of the peer", "Path to the device",
                                   std::nullopt, std::nullopt, std::nullopt};
    help.network_rtpmidi_client_t = router_create_help_entry_t{
        "Name of the peer", std::nullopt, "Hostname of the server",
        "Port of the server", std::nullopt};
    std::string out;
    jsondm::serialize(help, out);
    respond(cmd, "result", out);
  } else {
    throw std::runtime_error("Unknown peer type or not constructible yet: " +
                            p.type);
  }
}

void control_connection_actor_t::cmd_mdns_remove(const pending_command_t &cmd,
                                                 std::string_view params) {
  mdns_remove_params_t p;
  jsondm::deserialize(params, p);
  if (!mdns_mailbox_) {
    respond_error(cmd, "mdns not available");
    return;
  }
  mdns_mailbox_->post_control(mdns_remove_t{
      p.name, p.hostname.value_or(""), p.port});
  respond(cmd, "result", "\"ok\"");
}

void control_connection_actor_t::cmd_export_rawmidi(
    const pending_command_t &cmd, std::string_view params) {
  export_rawmidi_params_t p;
  try {
    jsondm::deserialize(params, p);
  } catch (const jsondm::exception &) {
    std::string out;
    jsondm::serialize(
        export_error_t{"Need device",
                       export_help_t{"Path to the device. Mandatory.",
                                     "Name of the peer", "Local UDP port",
                                     "Remote UDP port",
                                     "Hostname of the server if want to "
                                     "connect to. Else is a local listener."}},
        out);
    respond_error(cmd, out);
    return;
  }
  if (!p.device || p.device->empty()) {
    std::string out;
    jsondm::serialize(export_error_t{"Need device", export_help_t{}},
                      out);
    respond_error(cmd, out);
    return;
  }
  const auto corr = next_corr();
  spawn_peer_t sp;
  sp.type = "local_rawmidi_peer_t";
  sp.meta = p.name.value_or("");
  sp.factory = [worker = worker_, name = p.name.value_or(""),
                device = *p.device](
                   const mailbox_handle_t &sup, peer_id_t pid) {
    const int fd = ::open(device.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
      throw std::runtime_error("cannot open " + device);
    }
    return std::make_shared<local_rawmidi_peer_actor_t>(
        actor_config_t{.name = name, .id = pid, .supervisor_mailbox = sup},
        device, name, fd);
  };
  spawn_via_router(cmd, corr, std::move(sp));
}

void control_connection_actor_t::cmd_help(const pending_command_t &cmd) {
  std::vector<command_help_t> res;
  res.push_back({"status", "Return status of the daemon"});
  res.push_back({"router.remove", "Remove a peer from the router"});
  res.push_back({"router.connect", "Connect two peers (unidirectional)"});
  res.push_back({"router.disconnect", "Disconnect two peers"});
  res.push_back({"connect", "Connect to a remote rtpmidi endpoint"});
  res.push_back({"router.create", "Create a peer of the given type"});
  res.push_back({"mdns.remove", "Delete a mdns announcement"});
  res.push_back({"export.rawmidi", "Exports a rawmidi device"});
  res.push_back({"help", "Return help text"});
  std::string out;
  jsondm::serialize(res, out);
  respond(cmd, "result", out);
}

void control_connection_actor_t::cmd_peer_command(
    const pending_command_t &cmd, const std::string &peer_id_str,
    const std::string &peer_cmd, std::string_view params) {
  peer_id_t peer_id = 0;
  try {
    peer_id = (peer_id_str.empty()) ? 0 : std::stoul(peer_id_str);
  } catch (const std::exception &) {
    respond_error(cmd, FMT::format("Bad peer id '{}'", peer_id_str));
    return;
  }
  const auto corr = next_corr();
  router_mailbox_->post_control(peer_command_t{
      hdr_t{corr}, mailbox(), peer_id, peer_cmd, std::string(params)});
  wait_for(
      [corr](const control_message_t &m) {
        return std::holds_alternative<peer_command_resp_t>(m.v) &&
               std::get<peer_command_resp_t>(m.v).hdr.corr == corr;
      },
      request_deadline_,
      [this, cmd](std::optional<control_message_t> res) {
        if (!res) {
          respond_error(cmd, "deadline: no response from peer");
          return;
        }
        auto &r = std::get<peer_command_resp_t>(res->v);
        if (r.is_error) {
          respond_error(cmd, r.result_json);
        } else {
          respond(cmd, "result", r.result_json);
        }
      });
}

void control_connection_actor_t::notify_gone() { gone_ = true; }

// ---------------------------------------------------------------------------
// Listener
// ---------------------------------------------------------------------------

control_listener_actor_t::control_listener_actor_t(
    actor_config_t config, std::string socket_path,
    mailbox_handle_t router_mailbox, mailbox_handle_t mdns_mailbox,
    std::shared_ptr<worker_actor_t> worker, std::string version,
    std::chrono::milliseconds request_deadline)
    : actor_t(std::move(config)), socket_path_(std::move(socket_path)),
      router_mailbox_(std::move(router_mailbox)),
      mdns_mailbox_(std::move(mdns_mailbox)), worker_(std::move(worker)),
      version_(std::move(version)), request_deadline_(request_deadline) {}

void control_listener_actor_t::on_start() {
  const std::string &socketfile = socket_path_;
  ::unlink(socketfile.c_str());
  listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd_ == -1) {
    ERROR("Control listener: socket: {}", strerror(errno));
    return;
  }
  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socketfile.c_str(), sizeof(addr.sun_path) - 1);
  if (::bind(listen_fd_, (const struct sockaddr *)(&addr),
             sizeof(struct sockaddr_un)) != 0) {
    ERROR("Control listener: bind {}: {}", socketfile, strerror(errno));
    ::close(listen_fd_);
    listen_fd_ = -1;
    return;
  }
  if (::listen(listen_fd_, 20) != 0) {
    ERROR("Control listener: listen: {}", strerror(errno));
    ::close(listen_fd_);
    listen_fd_ = -1;
    return;
  }
  ::chmod(socketfile.c_str(), 0777);
  listener_ = add_fd_in(listen_fd_, [this](int) { accept_client(); });

  INFO("Control listener ready at {}", socketfile);
}

void control_listener_actor_t::on_stop() {
  for (auto &c : connections_) {
    c->request_stop_token();
  }
  connections_.clear();
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
}

void control_listener_actor_t::accept_client() {
  const int fd = ::accept(listen_fd_, nullptr, nullptr);
  if (fd == -1) {
    ERROR("Control listener: accept: {}", strerror(errno));
    return;
  }

  auto conn = std::make_shared<control_connection_actor_t>(
      actor_config_t{.name = FMT::format("control-conn-{}", fd),
                     .id = uint32_t(fd),
                     .supervisor_mailbox = mailbox()},
      fd, router_mailbox_, mdns_mailbox_, worker_, version_,
      request_deadline_);
  conn->start();
  connections_.push_back(std::move(conn));
}

void control_listener_actor_t::on_control(control_message_t &&msg) {
  std::visit(
      [this](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, stopped_t>) {
          // A connection actor exited: drop it (join is safe: stopped).
          std::erase_if(connections_, [&](const auto &c) {
            return c->id() == m.peer_id;
          });
        }
      },
      msg.v);
  (void)0;
}

} // namespace rtpmididns

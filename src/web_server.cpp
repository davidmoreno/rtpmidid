/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2025 David Moreno Montero <dmoreno@coralbits.com>
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
#include "web_server.hpp"
#include "aseq.hpp"
#include "control_rpc.hpp"
#include "midirouter.hpp"
#include "settings.hpp"
#include "dm_json_generated.hpp"
#include "dm_json_rpc.hpp"
#include <rtpmidid/dm_json/runtime.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/mdns_rtpmidi.hpp>

#include <cctype>
#include <cstring>

#include "third_party/httplib.h"

namespace rtpmididns {

namespace {

static const char *B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool base64_decode(const std::string &in, std::string &out) {
  out.clear();
  int val = 0;
  int valb = -8;
  for (unsigned char c : in) {
    if (std::isspace(c) != 0) {
      continue;
    }
    if (c == '=') {
      break;
    }
    const char *p = std::strchr(B64, static_cast<char>(c));
    if (p == nullptr) {
      return false;
    }
    val = (val << 6) + static_cast<int>(p - B64);
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return true;
}

bool check_basic_auth(const httplib::Request &req, const std::string &user,
                      const std::string &pass) {
  if (user.empty() && pass.empty()) {
    return true;
  }
  const std::string h = req.get_header_value("Authorization");
  constexpr const char *pfx = "Basic ";
  if (h.size() < 7 || h.compare(0, 6, pfx) != 0) {
    return false;
  }
  std::string decoded;
  if (!base64_decode(h.substr(6), decoded)) {
    return false;
  }
  const auto colon = decoded.find(':');
  if (colon == std::string::npos) {
    return false;
  }
  return decoded.substr(0, colon) == user && decoded.substr(colon + 1) == pass;
}

bool auth_ws_first_frame(std::string_view msg, const std::string &user,
                         const std::string &pass) {
  dmjson::rpc::envelope_info_t env;
  std::string err;
  if (!dmjson::rpc::scan_envelope(msg, env, err))
    return false;
  if (env.method != "_auth")
    return false;
  ws_auth_params_t p{};
  const std::string_view pj =
      env.has_params ? env.params_json : std::string_view("{}");
  if (!dmjson::from_json(pj, p))
    return false;
  return p.username == user && p.password == pass;
}

} // namespace

void web_server_t::stop() {
  httplib::Server *p = srv_.load(std::memory_order_acquire);
  if (p != nullptr) {
    p->stop();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

web_server_t::~web_server_t() { stop(); }

void web_server_t::start() {
  if (!settings.web.enabled) {
    DEBUG("Web UI disabled (settings.web.enabled=false)");
    return;
  }
  if (thread_.joinable()) {
    return;
  }
  running_.store(true, std::memory_order_release);
  try {
    thread_ = std::thread(&web_server_t::thread_main, this);
  } catch (const std::exception &e) {
    running_.store(false, std::memory_order_release);
    ERROR("Could not start web server thread: {}", e.what());
  }
}

void web_server_t::thread_main() {
  const std::string root = settings.web.root;
  const std::string listen = settings.web.listen;
  const int port = settings.web.port;
  const std::string user = settings.web.username;
  const std::string pass = settings.web.password;
  const bool need_auth = !user.empty() && !pass.empty();

  auto svr = std::make_unique<httplib::Server>();
  httplib::Server *const raw = svr.get();
  srv_.store(raw, std::memory_order_release);

  svr->set_mount_point("/", root);

  svr->set_pre_routing_handler(
      [&](const httplib::Request &req, httplib::Response &res) {
        if (req.path == "/ws") {
          return httplib::Server::HandlerResponse::Unhandled;
        }
        if (!need_auth) {
          return httplib::Server::HandlerResponse::Unhandled;
        }
        if (check_basic_auth(req, user, pass)) {
          return httplib::Server::HandlerResponse::Unhandled;
        }
        res.status = 401;
        res.set_header("WWW-Authenticate", "Basic realm=\"rtpmidid\"");
        res.set_content("Unauthorized", "text/plain");
        return httplib::Server::HandlerResponse::Handled;
      });

  svr->WebSocket("/ws", [&](const httplib::Request &req, httplib::ws::WebSocket &ws) {
    control_rpc_context_t ctx{router, aseq, mdns};
    bool authed = !need_auth || check_basic_auth(req, user, pass);

    while (ws.is_open()) {
      std::string msg;
      const auto rr = ws.read(msg);
      if (rr == httplib::ws::ReadResult::Fail) {
        break;
      }
      if (rr != httplib::ws::ReadResult::Text) {
        continue;
      }
      dmjson::rpc::envelope_info_t env;
      std::string perr;
      if (!dmjson::rpc::scan_envelope(msg, env, perr)) {
        dmjson::writer_t ew;
        ew.begin_object();
        ew.key("error");
        ew.string_value(perr);
        ew.end_object();
        std::string es;
        ew.swap_into_string(es);
        (void)ws.send(es);
        continue;
      }

      if (!authed) {
        if (need_auth && auth_ws_first_frame(msg, user, pass)) {
          authed = true;
          dmjson::writer_t okw;
          okw.begin_object();
          okw.key("id");
          if (env.has_id)
            okw.raw(env.id_json);
          else
            okw.raw("null");
          okw.key("result");
          okw.string_value("ok");
          okw.end_object();
          std::string os;
          okw.swap_into_string(os);
          (void)ws.send(os);
          continue;
        }
        dmjson::writer_t ew;
        ew.begin_object();
        ew.key("id");
        if (env.has_id)
          ew.raw(env.id_json);
        else
          ew.raw("null");
        ew.key("error");
        ew.string_value("unauthorized");
        ew.end_object();
        std::string es;
        ew.swap_into_string(es);
        (void)ws.send(es);
        break;
      }

      (void)ws.send(control_rpc_dispatch_line(ctx, msg));
    }
  });

  INFO("Web UI listening on http://{}:{}/ (WS /ws), root={}", listen, port,
       root);
  const bool ok = svr->listen(listen.c_str(), port);
  if (!ok) {
    ERROR("Web UI failed to listen on {}:{} (root={})", listen, port, root);
  }

  srv_.store(nullptr, std::memory_order_release);
  svr.reset();
  running_.store(false, std::memory_order_release);
}

} // namespace rtpmididns

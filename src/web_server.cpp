/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2025 David Moreno Montero <dmoreno@coralbits.com>
 */
#include "web_server.hpp"
#include "aseq.hpp"
#include "control_rpc.hpp"
#include "midirouter.hpp"
#include "settings.hpp"
#include "json.hpp"
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

bool auth_ws_first_frame(const json_t &js, const std::string &user,
                         const std::string &pass) {
  if (!js.contains("method") || !js["method"].is_string()) {
    return false;
  }
  if (js["method"].get<std::string>() != "_auth") {
    return false;
  }
  const auto &p = js["params"];
  if (!p.is_object()) {
    return false;
  }
  return p.value("username", "") == user && p.value("password", "") == pass;
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
      json_t js;
      try {
        js = json_t::parse(msg);
      } catch (const std::exception &e) {
        json_t err{{"error", e.what()}};
        (void)ws.send(err.dump());
        continue;
      }

      if (!authed) {
        if (need_auth && auth_ws_first_frame(js, user, pass)) {
          authed = true;
          json_t ok;
          if (js.contains("id")) {
            ok["id"] = js["id"];
          }
          ok["result"] = "ok";
          (void)ws.send(ok.dump());
          continue;
        }
        json_t err;
        if (js.contains("id")) {
          err["id"] = js["id"];
        }
        err["error"] = "unauthorized";
        (void)ws.send(err.dump());
        break;
      }

      json_t out = control_rpc_dispatch(ctx, js);
      (void)ws.send(out.dump());
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

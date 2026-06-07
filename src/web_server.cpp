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
#include "event_subscription.hpp"
#include "midirouter.hpp"
#include "settings.hpp"
#include "webui_midi_monitor_peer.hpp"
#include "dm_json_generated.hpp"
#include "dm_json_rpc.hpp"
#include "dm_json_status.hpp"
#include <rtpmidid/dm_json/runtime.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <rtpmidid/shutdown_signals.hpp>
#include <rtpmidid/signal.hpp>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>

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

/** Short hex preview for WebSocket binary/text debug logs (cap length). */
std::string monitor_ws_hex_preview(const uint8_t *data, size_t len,
                                  size_t max_bytes = 48) {
  std::string out;
  const size_t n = len < max_bytes ? len : max_bytes;
  out.reserve(n * 3 + 32);
  for (size_t i = 0; i < n; ++i) {
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02x%s", data[i],
                  (i + 1 < n) ? " " : "");
    out += buf;
  }
  if (len > max_bytes) {
    char tail[64];
    std::snprintf(tail, sizeof(tail), " … (total %zu bytes)", len);
    out += tail;
  }
  return out;
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

web_server_t::ws_shutdown_registration::ws_shutdown_registration(
    web_server_t &server, std::function<void()> close_fn)
    : server_(&server) {
  slot_ = std::make_shared<ws_shutdown_slot_t>();
  slot_->close = std::move(close_fn);
  server.register_ws_shutdown(slot_);
}

web_server_t::ws_shutdown_registration::~ws_shutdown_registration() {
  if (server_ != nullptr && slot_) {
    server_->unregister_ws_shutdown(slot_.get());
  }
}

web_server_t::ws_shutdown_registration::ws_shutdown_registration(
    ws_shutdown_registration &&other) noexcept
    : server_(other.server_), slot_(std::move(other.slot_)) {
  other.server_ = nullptr;
}

web_server_t::ws_shutdown_registration &
web_server_t::ws_shutdown_registration::operator=(
    ws_shutdown_registration &&other) noexcept {
  if (this != &other) {
    if (server_ != nullptr && slot_) {
      server_->unregister_ws_shutdown(slot_.get());
    }
    server_ = other.server_;
    slot_ = std::move(other.slot_);
    other.server_ = nullptr;
  }
  return *this;
}

void web_server_t::register_ws_shutdown(
    std::shared_ptr<ws_shutdown_slot_t> slot) {
  std::lock_guard<std::mutex> lock(active_ws_mutex_);
  active_ws_.push_back(std::move(slot));
}

void web_server_t::unregister_ws_shutdown(const ws_shutdown_slot_t *slot) {
  std::lock_guard<std::mutex> lock(active_ws_mutex_);
  active_ws_.erase(
      std::remove_if(active_ws_.begin(), active_ws_.end(),
                     [slot](const std::shared_ptr<ws_shutdown_slot_t> &s) {
                       return s.get() == slot;
                     }),
      active_ws_.end());
}

void web_server_t::close_all_active_ws() {
  std::vector<std::function<void()>> closers;
  {
    std::lock_guard<std::mutex> lock(active_ws_mutex_);
    closers.reserve(active_ws_.size());
    for (const auto &slot : active_ws_) {
      if (slot && slot->close) {
        closers.push_back(slot->close);
      }
    }
  }
  for (auto &close_fn : closers) {
    close_fn();
  }
}

void web_server_t::stop() {
  if (!thread_.joinable()) {
    return;
  }

  shutting_down_.store(true, std::memory_order_release);
  close_all_active_ws();

  httplib::Server *p = srv_.load(std::memory_order_acquire);
  if (p != nullptr) {
    p->stop();
  }
  if (thread_.joinable()) {
    thread_.join();
  }

  shutting_down_.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(active_ws_mutex_);
    active_ws_.clear();
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
    // Wire stats collector to router callbacks before starting threads
    if (router) {
      router->on_peer_sent = stats_collector.sent_callback();
      router->on_peer_recv = stats_collector.recv_callback();
      router->get_stats_sent = [this](peer_id_t pid) {
        return stats_collector.get_sent(pid);
      };
      router->get_stats_recv = [this](peer_id_t pid) {
        return stats_collector.get_recv(pid);
      };
      router->on_peer_registered = [this](peer_id_t pid) {
        stats_collector.register_peer(pid);
      };
      router->on_peer_unregistered = [this](peer_id_t pid) {
        stats_collector.unregister_peer(pid);
      };
    }
    stats_collector.start();
    thread_ = std::thread(&web_server_t::thread_main, this);
  } catch (const std::exception &e) {
    running_.store(false, std::memory_order_release);
    ERROR("Could not start web server thread: {}", e.what());
  }
}

void web_server_t::thread_main() {
  rtpmidid::block_shutdown_signals();

  const std::string root = settings.web.root;
  const std::string listen = settings.web.listen;
  const int port = settings.web.port;
  const std::string user = settings.web.username;
  const std::string pass = settings.web.password;
  const bool need_auth = !user.empty() && !pass.empty();
  const std::shared_ptr<midirouter_t> router_for_monitor = router;

  auto svr = std::make_unique<httplib::Server>();
  httplib::Server *const raw = svr.get();
  srv_.store(raw, std::memory_order_release);

  svr->set_logger(
      [](const httplib::Request &req, const httplib::Response &res) {
        const char *path_or_target =
            req.target.empty() ? req.path.c_str() : req.target.c_str();
        INFO("HTTP {} {} — {} {}:{}", req.method, path_or_target, res.status,
             req.remote_addr, req.remote_port);
      });
  svr->set_error_logger([](const httplib::Error &err,
                           const httplib::Request *req) {
    if (req != nullptr) {
      WARNING("HTTP error {} {} {} — {}:{}", httplib::to_string(err),
              req->method,
              req->target.empty() ? req->path : req->target, req->remote_addr,
              req->remote_port);
    } else {
      WARNING("HTTP error {}", httplib::to_string(err));
    }
  });

  svr->set_mount_point("/", root);

  svr->set_pre_routing_handler(
      [&](const httplib::Request &req, httplib::Response &res) {
        if (req.path == "/ws" || req.path == "/ws/monitor") {
          /* Successful WS upgrades do not go through write_response(), so
           * set_logger never sees HTTP 101 — log upgrade attempts here. */
          const std::string uuid =
              req.path == "/ws/monitor" ? req.get_param_value("uuid") : "";
          const char *up = req.has_header("Upgrade")
                               ? req.get_header_value("Upgrade").c_str()
                               : "";
          const char *conn = req.has_header("Connection")
                                 ? req.get_header_value("Connection").c_str()
                                 : "";
          const char *wsver = req.has_header("Sec-WebSocket-Version")
                                  ? req.get_header_value("Sec-WebSocket-Version")
                                        .c_str()
                                  : "";
          const size_t key_len = req.has_header("Sec-WebSocket-Key")
                                     ? req.get_header_value("Sec-WebSocket-Key")
                                           .size()
                                     : 0;
          INFO("Web UI WS pre-route {} target={} from {}:{} "
               "uuid={} Upgrade={} Connection={} Sec-WebSocket-Key_len={} "
               "Sec-WebSocket-Version={} need_http_auth={}",
               req.method,
               req.target.empty() ? req.path : req.target, req.remote_addr,
               req.remote_port, uuid.empty() ? std::string("-") : uuid, up,
               conn, key_len, wsver, need_auth);
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

  svr->WebSocket("/ws", [this, need_auth, user, pass](const httplib::Request &req,
                                                      httplib::ws::WebSocket &ws) {
    // ── Per-connection event subscription ────────────────────────────
    auto subs = std::make_shared<event_subscription_manager_t>();
    subs->set_send_fn([&ws](const std::string &json) {
      if (ws.is_open())
        ws.send(json);
    });
    stats_collector.register_subscriber(subs);

    // RAII guards for signal → event forwarding.
    // Each connection_t disconnects automatically on destruction.
    struct signal_guards_t {
      ::rtpmidid::connection_t<peer_id_t> peer_added;
      ::rtpmidid::connection_t<peer_id_t> peer_removed;
      ::rtpmidid::connection_t<peer_id_t, peer_id_t> edge_added;
      ::rtpmidid::connection_t<peer_id_t, peer_id_t> edge_removed;
      ::rtpmidid::connection_t<const std::string &, const std::string &,
                                const std::string &>
          mdns_discovered;
      ::rtpmidid::connection_t<const std::string &, const std::string &,
                                const std::string &>
          mdns_removed;
    };
    auto guards = std::make_shared<signal_guards_t>();

    // Connect router signals → event channels
    if (router) {
      guards->peer_added = router->peer_added_event.connect(
          [subs, router = router](peer_id_t pid) {
            auto rows = router->status_rows();
            for (const auto &r : rows) {
              if (r.id && static_cast<peer_id_t>(*r.id) == pid) {
                subs->emit("router.peer_added", dmjson::to_json(r));
                break;
              }
            }
          });

      guards->peer_removed = router->peer_removed_event.connect(
          [subs](peer_id_t pid) {
            router_peer_removed_event_t evt;
            evt.peer_id = static_cast<uint64_t>(pid);
            subs->emit("router.peer_removed", dmjson::to_json(evt));
          });

      guards->edge_added = router->connected_event.connect(
          [subs](peer_id_t from, peer_id_t to) {
            router_edge_event_t evt;
            evt.from = static_cast<uint64_t>(from);
            evt.to = static_cast<uint64_t>(to);
            subs->emit("router.edge_added", dmjson::to_json(evt));
          });

      guards->edge_removed = router->disconnected_event.connect(
          [subs](peer_id_t from, peer_id_t to) {
            router_edge_event_t evt;
            evt.from = static_cast<uint64_t>(from);
            evt.to = static_cast<uint64_t>(to);
            subs->emit("router.edge_removed", dmjson::to_json(evt));
          });
    }

    // Connect mDNS signals → event channels
    if (mdns) {
      guards->mdns_discovered = mdns->discover_event.connect(
          [subs](const std::string &name, const std::string &address,
                  const std::string &port_str) {
            mdns_remote_row_t evt;
            evt.name = name;
            evt.hostname = address;
            evt.ip = address;
            evt.port = static_cast<uint32_t>(std::stoul(port_str.empty() ? "0" : port_str));
            subs->emit("mdns.discovered", dmjson::to_json(evt));
          });

      guards->mdns_removed = mdns->remove_event.connect(
          [subs](const std::string &name, const std::string &address,
                  const std::string &port_str) {
            mdns_removed_event_t evt;
            evt.name = name;
            evt.address = address;
            evt.port = static_cast<uint32_t>(std::stoul(port_str.empty() ? "0" : port_str));
            subs->emit("mdns.removed", dmjson::to_json(evt));
          });
    }

    // ── RPC dispatch ──────────────────────────────────────────────────
    control_rpc_context_t ctx{router, aseq, mdns, connection_db, device_registry,
                               subs};
    bool authed = !need_auth || check_basic_auth(req, user, pass);

    ws_shutdown_registration ws_reg(*this, [&ws]() {
      if (ws.is_open()) {
        ws.close(httplib::ws::CloseStatus::GoingAway, "server shutdown");
      }
    });

    while (ws.is_open() && !is_shutting_down()) {
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

  /* Binary MIDI stream for `monitor.start` sessions; query ?uuid=… */
  svr->WebSocket("/ws/monitor", [this, router_for_monitor, need_auth, user, pass](
                                    const httplib::Request &req,
                                    httplib::ws::WebSocket &ws) {
    const std::string uuid_q = req.get_param_value("uuid");
    INFO("Web UI WS /ws/monitor upgraded (handshake ok) from {}:{} uuid={}",
         req.remote_addr, req.remote_port,
         uuid_q.empty() ? std::string("-") : uuid_q);

    bool authed = !need_auth || check_basic_auth(req, user, pass);
    if (!authed) {
      WARNING("Web UI WS /ws/monitor closing: HTTP Basic auth required "
              "(check browser credentials for ws:// same origin) from {}:{}",
              req.remote_addr, req.remote_port);
      ws.close(httplib::ws::CloseStatus::PolicyViolation, "unauthorized");
      return;
    }
    const std::string uuid = uuid_q;
    if (uuid.empty()) {
      WARNING("Web UI WS /ws/monitor closing: missing ?uuid= from {}:{}",
              req.remote_addr, req.remote_port);
      ws.close(httplib::ws::CloseStatus::InvalidPayload, "need ?uuid=");
      return;
    }
    std::shared_ptr<webui_midi_monitor_peer_t> mon;
    for (int attempt = 0; attempt < 40 && !mon; ++attempt) {
      mon = monitor_registry_lookup(uuid);
      if (!mon && attempt < 39)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (!mon) {
      WARNING(
          "Web UI WS /ws/monitor closing: unknown monitor session uuid={} "
          "(call monitor.start first, or session expired) from {}:{}",
          uuid, req.remote_addr, req.remote_port);
      ws.close(httplib::ws::CloseStatus::InvalidPayload, "unknown session");
      return;
    }

    INFO("Web UI WS /ws/monitor streaming MIDI for uuid={} from {}:{} "
         "(router pushes frames via sink; per-frame DEBUG)",
         uuid, req.remote_addr, req.remote_port);

    ws_shutdown_registration ws_reg(*this, [&ws]() {
      if (ws.is_open()) {
        ws.close(httplib::ws::CloseStatus::GoingAway, "server shutdown");
      }
    });

    using namespace std::chrono_literals;
    uint64_t monitor_ws_sent_bytes = 0;
    uint64_t monitor_ws_recv_bytes = 0;
    uint64_t monitor_ws_send_frames = 0;
    uint64_t monitor_ws_recv_frames = 0;

    /* One viewer per monitor session; router thread calls ws.send via sink. */
    if (!mon->try_set_ws_binary_sink([&](const uint8_t *data, size_t len) {
          const bool ok =
              ws.send(reinterpret_cast<const char *>(data), len);
          if (ok) {
            monitor_ws_sent_bytes += len;
            monitor_ws_send_frames += 1;
            DEBUG("Web UI WS monitor uuid={} → client binary frame #{} {} "
                  "bytes [{}]",
                  uuid, monitor_ws_send_frames, len,
                  monitor_ws_hex_preview(data, len));
          }
          return ok;
        })) {
      WARNING(
          "Web UI WS /ws/monitor rejected second viewer uuid={} from {}:{} "
          "(stop other tab or monitor.stop + monitor.start new session)",
          uuid, req.remote_addr, req.remote_port);
      ws.close(httplib::ws::CloseStatus::PolicyViolation,
               "monitor viewer already connected");
      return;
    }

    {
      struct monitor_sink_guard_t {
        std::shared_ptr<webui_midi_monitor_peer_t> peer;
        ~monitor_sink_guard_t() {
          if (peer)
            peer->clear_ws_binary_sink();
        }
      } sink_guard{mon};

      while (ws.is_open() && !is_shutting_down()) {
        std::string msg;
        const auto rr = ws.read(msg);
        if (rr == httplib::ws::ReadResult::Fail) {
          DEBUG(
              "Web UI WS monitor uuid={} read finished (close/fail); sent {} "
              "bytes in {} frames, recv {} bytes in {} frames",
              uuid, monitor_ws_sent_bytes, monitor_ws_send_frames,
              monitor_ws_recv_bytes, monitor_ws_recv_frames);
          break;
        }
        if (is_shutting_down()) {
          break;
        }
        monitor_ws_recv_bytes += msg.size();
        monitor_ws_recv_frames += 1;
        DEBUG(
            "Web UI WS monitor uuid={} ← client {} frame #{} {} bytes [{}]",
            uuid, rr == httplib::ws::ReadResult::Text ? "text" : "binary",
            monitor_ws_recv_frames, msg.size(),
            monitor_ws_hex_preview(reinterpret_cast<const uint8_t *>(msg.data()),
                                   msg.size()));
        std::this_thread::sleep_for(2ms);
      }
      INFO("Web UI WS /ws/monitor connection ended uuid={} from {}:{} "
           "(sent {} bytes / {} frames, received {} bytes / {} ws frames); "
           "session stays active until monitor.stop",
           uuid, req.remote_addr, req.remote_port, monitor_ws_sent_bytes,
           monitor_ws_send_frames, monitor_ws_recv_bytes, monitor_ws_recv_frames);
    }
  });

  INFO("Web UI listening on http://{}:{}/ (WS /ws, /ws/monitor), root={}",
       listen, port, root);
  const bool ok = svr->listen(listen.c_str(), port);
  if (!ok) {
    ERROR("Web UI failed to listen on {}:{} (root={})", listen, port, root);
  }

  srv_.store(nullptr, std::memory_order_release);
  svr.reset();
  running_.store(false, std::memory_order_release);
}

} // namespace rtpmididns

/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2025 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#include "web_control_server.hpp"
#include "control_socket.hpp"
#include "rtpmidid/logger.hpp"
#include "settings.hpp"
#include "stringpp.hpp"
#include <httplib.h>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <optional>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace rtpmididns {

namespace {

constexpr const char *kSessionCookie = "rtpmidid_session";

#ifndef RTPMIDID_WEB_INSTALL_DIR
#define RTPMIDID_WEB_INSTALL_DIR ""
#endif
#ifndef RTPMIDID_WEB_STAGING_DIR
#define RTPMIDID_WEB_STAGING_DIR ""
#endif

bool constant_time_eq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  unsigned char d = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    d |= static_cast<unsigned char>(a[i] ^ b[i]);
  }
  return d == 0;
}

std::optional<std::string> cookie_value(const std::string &hdr,
                                        std::string_view name) {
  const std::string needle = std::string(name) + "=";
  size_t pos = 0;
  while (pos < hdr.size()) {
    while (pos < hdr.size() && (hdr[pos] == ' ' || hdr[pos] == ';')) {
      ++pos;
    }
    size_t end = hdr.find(';', pos);
    if (end == std::string::npos) {
      end = hdr.size();
    }
    std::string_view part(hdr.data() + pos, end - pos);
    if (part.size() >= needle.size() &&
        part.compare(0, needle.size(), needle) == 0) {
      std::string_view v = part.substr(needle.size());
      while (!v.empty() && v.back() == ' ') {
        v.remove_suffix(1);
      }
      return std::string(v);
    }
    pos = end + 1;
  }
  return std::nullopt;
}

bool auth_enabled() {
  return !settings.web_user.empty() && !settings.web_password.empty();
}

std::string random_session_token() {
  unsigned char buf[16];
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    ERROR("open /dev/urandom: {}", strerror(errno));
    return {};
  }
  ssize_t n = read(fd, buf, sizeof(buf));
  close(fd);
  if (n != static_cast<ssize_t>(sizeof(buf))) {
    return {};
  }
  static const char *hex = "0123456789abcdef";
  std::string out;
  out.resize(sizeof(buf) * 2);
  for (size_t i = 0; i < sizeof(buf); ++i) {
    out[i * 2] = hex[buf[i] >> 4];
    out[i * 2 + 1] = hex[buf[i] & 0xf];
  }
  return out;
}

#ifdef __linux__
std::optional<fs::path> exe_dir_linux() {
  std::string path;
  path.resize(static_cast<size_t>(PATH_MAX));
  ssize_t n = readlink("/proc/self/exe", path.data(), path.size());
  if (n <= 0) {
    return std::nullopt;
  }
  path.resize(static_cast<size_t>(n));
  return fs::path(path).parent_path();
}
#endif

fs::path resolve_web_root() {
  if (const char *env = std::getenv("RTPMIDID_WEB_ROOT")) {
    if (env[0] != '\0') {
      fs::path p(env);
      if (fs::is_directory(p)) {
        return fs::weakly_canonical(fs::absolute(p));
      }
    }
  }
#ifdef __linux__
  if (auto exed = exe_dir_linux()) {
    fs::path share =
        fs::weakly_canonical(fs::absolute(*exed / ".." / "share" / "rtpmidid" /
                                           "web"));
    if (fs::is_directory(share)) {
      return share;
    }
  }
#endif
  if constexpr (sizeof(RTPMIDID_WEB_STAGING_DIR) > 1) {
    fs::path stage(RTPMIDID_WEB_STAGING_DIR);
    if (fs::is_directory(stage)) {
      return fs::weakly_canonical(fs::absolute(stage));
    }
  }
  if constexpr (sizeof(RTPMIDID_WEB_INSTALL_DIR) > 1) {
    fs::path inst(RTPMIDID_WEB_INSTALL_DIR);
    if (fs::is_directory(inst)) {
      return fs::weakly_canonical(fs::absolute(inst));
    }
  }
  if constexpr (sizeof(RTPMIDID_WEB_STAGING_DIR) > 1) {
    return fs::path(RTPMIDID_WEB_STAGING_DIR);
  }
  return fs::path(RTPMIDID_WEB_INSTALL_DIR);
}

bool path_under_root(const fs::path &root, const fs::path &file) {
  std::error_code ec;
  fs::path rc = fs::weakly_canonical(fs::absolute(root), ec);
  fs::path fc = fs::weakly_canonical(fs::absolute(file), ec);
  if (ec) {
    return false;
  }
  auto rel = fc.lexically_relative(rc);
  for (const auto &part : rel) {
    if (part == "..") {
      return false;
    }
  }
  return true;
}

std::string read_text_file(const fs::path &p) {
  std::ifstream in(p, std::ios::in | std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void send_file(httplib::Response &res, const fs::path &root,
               const std::string &rel, const char *mime) {
  fs::path full = fs::absolute(root) / rel;
  if (!path_under_root(root, full)) {
    res.status = httplib::StatusCode::Forbidden_403;
    return;
  }
  if (!fs::is_regular_file(full)) {
    res.status = httplib::StatusCode::NotFound_404;
    return;
  }
  auto body = read_text_file(full);
  if (body.empty() && fs::file_size(full) > 0) {
    res.status = httplib::StatusCode::InternalServerError_500;
    return;
  }
  res.set_content(body, mime);
}

} // namespace

bool web_control_server_t::cookie_session_valid(const httplib::Request &req) {
  if (!auth_enabled()) {
    return true;
  }
  auto c = req.get_header_value("Cookie");
  if (c.empty()) {
    return false;
  }
  auto tok = cookie_value(c, kSessionCookie);
  if (!tok || tok->empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(sessions_mu_);
  return sessions_.count(*tok) != 0;
}

void web_control_server_t::remember_session(const std::string &token) {
  std::lock_guard<std::mutex> lock(sessions_mu_);
  sessions_.insert(token);
}

web_control_server_t::web_control_server_t(control_socket_t *control)
    : control_(control) {}

web_control_server_t::~web_control_server_t() { stop(); }

void web_control_server_t::stop() {
  stopping_.store(true);
  if (svr_) {
    svr_->stop();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  svr_.reset();
  stopping_.store(false);
}

void web_control_server_t::start() {
  if (settings.web_port <= 0 || settings.web_port > 65535) {
    return;
  }

  fs::path web_root = resolve_web_root();
  if (!fs::is_directory(web_root)) {
    ERROR("Web UI root is not a directory: {} (set RTPMIDID_WEB_ROOT or run "
          "cmake build so web assets are staged)",
          web_root.string());
    return;
  }

  svr_ = std::make_unique<httplib::Server>();
  httplib::Server &svr = *svr_;

  svr.set_pre_routing_handler(
      [this](const httplib::Request &req, httplib::Response &res) {
        if (!auth_enabled()) {
          return httplib::Server::HandlerResponse::Unhandled;
        }

        const std::string &path = req.path;
        const bool ws = httplib::detail::is_websocket_upgrade(req);

        const bool allow_unauth =
            path == "/login.html" || path == "/login" ||
            (req.method == "POST" && path == "/login") || path == "/logout";

        if (allow_unauth) {
          return httplib::Server::HandlerResponse::Unhandled;
        }

        if (!cookie_session_valid(req)) {
          if (ws && path == "/ws") {
            res.status = httplib::StatusCode::Unauthorized_401;
            res.set_content("Unauthorized\n", "text/plain");
            return httplib::Server::HandlerResponse::Handled;
          }
          if (req.method == "GET") {
            res.status = httplib::StatusCode::Found_302;
            res.set_header("Location", "/login.html");
            return httplib::Server::HandlerResponse::Handled;
          }
          res.status = httplib::StatusCode::Unauthorized_401;
          res.set_content("Unauthorized\n", "text/plain");
          return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
      });

  svr.Post("/login", [this, web_root](const httplib::Request &req,
                                      httplib::Response &res) {
    if (!auth_enabled()) {
      res.status = httplib::StatusCode::Found_302;
      res.set_header("Location", "/");
      return;
    }
    std::string user = req.get_param_value("username");
    std::string pass = req.get_param_value("password");
    if (!constant_time_eq(user, settings.web_user) ||
        !constant_time_eq(pass, settings.web_password)) {
      res.status = httplib::StatusCode::Unauthorized_401;
      res.set_content("Invalid username or password\n", "text/plain");
      return;
    }
    std::string tok = random_session_token();
    if (tok.empty()) {
      res.status = httplib::StatusCode::InternalServerError_500;
      res.set_content("Could not create session\n", "text/plain");
      return;
    }
    remember_session(tok);
    res.status = httplib::StatusCode::Found_302;
    res.set_header("Location", "/");
    res.set_header("Set-Cookie",
                   std::string(kSessionCookie) + "=" + tok +
                       "; Path=/; HttpOnly; SameSite=Lax; Max-Age=86400");
  });

  svr.Get("/logout", [this](const httplib::Request &req,
                            httplib::Response &res) {
    auto c = req.get_header_value("Cookie");
    auto tok = cookie_value(c, kSessionCookie);
    if (tok) {
      std::lock_guard<std::mutex> lock(sessions_mu_);
      sessions_.erase(*tok);
    }
    res.status = httplib::StatusCode::Found_302;
    res.set_header("Location", "/login.html");
    res.set_header("Set-Cookie",
                   std::string(kSessionCookie) +
                       "=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
  });

  svr.Get("/", [web_root](const httplib::Request & /*req*/,
                          httplib::Response &res) {
    send_file(res, web_root, "index.html", "text/html; charset=utf-8");
  });

  svr.Get("/index.html", [web_root](const httplib::Request & /*req*/,
                                    httplib::Response &res) {
    send_file(res, web_root, "index.html", "text/html; charset=utf-8");
  });

  svr.Get("/style.css", [web_root](const httplib::Request & /*req*/,
                                   httplib::Response &res) {
    send_file(res, web_root, "style.css", "text/css; charset=utf-8");
  });

  svr.Get("/help.html", [web_root](const httplib::Request & /*req*/,
                                   httplib::Response &res) {
    send_file(res, web_root, "help.html", "text/html; charset=utf-8");
  });

  svr.Get("/login", [](const httplib::Request & /*req*/,
                      httplib::Response &res) {
    res.status = httplib::StatusCode::Found_302;
    res.set_header("Location", "/login.html");
  });

  svr.Get("/login.html", [web_root](const httplib::Request & /*req*/,
                                   httplib::Response &res) {
    send_file(res, web_root, "login.html", "text/html; charset=utf-8");
  });

  if (!svr.set_mount_point("/vendor", (web_root / "vendor").string())) {
    ERROR("Could not mount /vendor static files");
    svr_.reset();
    return;
  }

  svr.WebSocket("/ws", [this](const httplib::Request & /*req*/,
                              httplib::ws::WebSocket &ws) {
    std::string msg;
    while (true) {
      auto r = ws.read(msg);
      if (r == httplib::ws::ReadResult::Fail) {
        break;
      }
      if (r == httplib::ws::ReadResult::Binary) {
        continue;
      }
      trim(msg);
      if (msg.size() >= 1024) {
        static constexpr const char *too_long =
            "{\"event\": \"close\", \"detail\": \"Message too long\", "
            "\"code\": 1}";
        ws.send(too_long);
        break;
      }
      if (msg.empty()) {
        continue;
      }
      std::string reply = control_->dispatch_command(msg);
      if (!ws.send(reply)) {
        break;
      }
    }
  });

  const std::string bind = settings.web_bind;
  const int port = settings.web_port;

  thread_ = std::thread([this, bind, port]() {
    INFO("Web control UI listening on http://{}:{}/", bind, port);
    if (!svr_->listen(bind.c_str(), port)) {
      ERROR("Web UI could not listen on {}:{}: {}", bind, port,
            strerror(errno));
    }
  });
}

} // namespace rtpmididns

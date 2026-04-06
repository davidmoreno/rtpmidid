/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2025 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include "rtpmidid/utils.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

namespace httplib {
class Request;
class Server;
}

namespace rtpmididns {
class control_socket_t;

/// Embedded HTTP server: static UI + WebSocket control (same JSON line protocol
/// as the Unix socket). See docs/WEB.md.
class web_control_server_t {
  NON_COPYABLE_NOR_MOVABLE(web_control_server_t)

public:
  explicit web_control_server_t(control_socket_t *control);
  ~web_control_server_t();

  void start();
  void stop();

private:
  bool cookie_session_valid(const httplib::Request &req);
  void remember_session(const std::string &token);

  control_socket_t *control_;
  std::unique_ptr<httplib::Server> svr_;
  std::thread thread_;
  std::mutex sessions_mu_;
  std::unordered_set<std::string> sessions_;
  std::atomic<bool> stopping_{false};
};

} // namespace rtpmididns

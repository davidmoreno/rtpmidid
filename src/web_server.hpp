/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2025 David Moreno Montero <dmoreno@coralbits.com>
 *
 * HTTP static UI + WebSocket JSON-RPC on a dedicated thread.
 */
#pragma once

#include "rtpmidid/utils.hpp"
#include <atomic>
#include <memory>
#include <thread>

namespace httplib {
class Server;
}

namespace rtpmidid {
class mdns_rtpmidi_t;
}

namespace rtpmididns {
class midirouter_t;
class aseq_t;

class web_server_t {
  NON_COPYABLE_NOR_MOVABLE(web_server_t)

  std::atomic<bool> running_{false};
  std::atomic<::httplib::Server *> srv_{nullptr};
  std::thread thread_;

  void thread_main();

public:
  std::shared_ptr<midirouter_t> router;
  std::shared_ptr<aseq_t> aseq;
  std::shared_ptr<rtpmidid::mdns_rtpmidi_t> mdns;

  web_server_t() = default;
  ~web_server_t();

  /** Stops HTTP listen and joins the server thread (safe if never started). */
  void stop();

  /** No-op if `settings.web.enabled` is false. */
  void start();
};

} // namespace rtpmididns

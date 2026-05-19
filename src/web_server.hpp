/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2025 David Moreno Montero <dmoreno@coralbits.com>
 *
 * HTTP static UI + WebSocket JSON-RPC on a dedicated thread.
 */
#pragma once

#include "rtpmidid/utils.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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
  std::atomic<bool> shutting_down_{false};
  std::atomic<::httplib::Server *> srv_{nullptr};
  std::thread thread_;

  struct ws_shutdown_slot_t {
    std::function<void()> close;
  };
  std::mutex active_ws_mutex_;
  std::vector<std::shared_ptr<ws_shutdown_slot_t>> active_ws_;

  void register_ws_shutdown(std::shared_ptr<ws_shutdown_slot_t> slot);
  void unregister_ws_shutdown(const ws_shutdown_slot_t *slot);
  void close_all_active_ws();

  void thread_main();

public:
  /** Closes an active WebSocket when the server shuts down (see stop()). */
  class ws_shutdown_registration {
    web_server_t *server_ = nullptr;
    std::shared_ptr<ws_shutdown_slot_t> slot_;

  public:
    ws_shutdown_registration() = default;
    ws_shutdown_registration(web_server_t &server, std::function<void()> close_fn);
    ~ws_shutdown_registration();
    ws_shutdown_registration(ws_shutdown_registration &&other) noexcept;
    ws_shutdown_registration &
    operator=(ws_shutdown_registration &&other) noexcept;
    ws_shutdown_registration(const ws_shutdown_registration &) = delete;
    ws_shutdown_registration &operator=(const ws_shutdown_registration &) = delete;
  };

  std::shared_ptr<midirouter_t> router;
  std::shared_ptr<aseq_t> aseq;
  std::shared_ptr<rtpmidid::mdns_rtpmidi_t> mdns;

  web_server_t() = default;
  ~web_server_t();

  bool is_shutting_down() const {
    return shutting_down_.load(std::memory_order_acquire);
  }

  /** Stops HTTP listen and joins the server thread (safe if never started). */
  void stop();

  /** No-op if `settings.web.enabled` is false. */
  void start();
};

} // namespace rtpmididns

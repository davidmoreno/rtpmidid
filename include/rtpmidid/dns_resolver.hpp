/**
 * Async DNS resolution (getaddrinfo) on a dedicated worker thread.
 * Completion callbacks run on the poller thread (via eventfd + call_later).
 */
#pragma once

#include <rtpmidid/networkaddress.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/utils.hpp>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rtpmidid {

class dns_resolver_t {
  NON_COPYABLE_NOR_MOVABLE(dns_resolver_t)

  struct job_t {
    std::string hostname;
    std::string port;
    std::function<void(network_address_list_t)> callback;
  };

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<job_t> pending_;
  std::deque<std::function<void()>> completions_;
  int eventfd_ = -1;
  poller_t::listener_t poll_listener_{};
  std::thread worker_;
  bool shutdown_{false};
  bool worker_started_{false};

  void ensure_started();
  void worker_loop();
  void on_eventfd_readable(int fd);
public:
  dns_resolver_t() = default;
  ~dns_resolver_t();

  /** Join worker and remove eventfd from poller (idempotent). */
  void stop_worker_and_fd();

  /**
   * Queue hostname:port resolution. The first hop of @p callback runs on the
   * poller thread (from the eventfd handler); callers typically use
   * poller.call_later inside it to defer work.
   */
  void resolve_async(std::string hostname, std::string port,
                     std::function<void(network_address_list_t)> callback);
};

/** Process-global resolver; lazily starts worker + poller integration. */
dns_resolver_t &dns_resolver();

/** Join DNS worker and unregister eventfd; call before poller.close(). */
void dns_resolver_shutdown();

} // namespace rtpmidid

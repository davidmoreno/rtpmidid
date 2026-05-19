/**
 * Async DNS: getaddrinfo on a worker thread; wake poller via eventfd.
 */
#include <rtpmidid/dns_resolver.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/shutdown_signals.hpp>
#include <cerrno>
#include <cstring>
#include <memory>
#include <sys/eventfd.h>
#include <unistd.h>

namespace rtpmidid {

void dns_resolver_t::ensure_started() {
  std::unique_lock<std::mutex> lk(mutex_);
  if (worker_started_ && worker_.joinable() && eventfd_ >= 0) {
    return;
  }

  shutdown_ = false;

  if (eventfd_ < 0) {
    const int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd < 0) {
      ERROR("dns_resolver: eventfd: {}", strerror(errno));
      return;
    }
    lk.unlock();
    poller_t::listener_t listener;
    try {
      listener = poller.add_fd_in(efd, [this](int fd) {
        on_eventfd_readable(fd);
      });
    } catch (const std::exception &e) {
      lk.lock();
      ::close(efd);
      ERROR("dns_resolver: add_fd_in: {}", e.what());
      return;
    }
    lk.lock();
    eventfd_ = efd;
    poll_listener_ = std::move(listener);
  }

  if (!worker_.joinable()) {
    lk.unlock();
    std::thread t([this] { worker_loop(); });
    lk.lock();
    worker_ = std::move(t);
    worker_started_ = true;
  }
}

void dns_resolver_t::worker_loop() {
  block_shutdown_signals();

  for (;;) {
    job_t job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] {
        return !pending_.empty() || shutdown_;
      });
      if (shutdown_ && pending_.empty()) {
        break;
      }
      if (pending_.empty()) {
        continue;
      }
      job = std::move(pending_.front());
      pending_.pop_front();
    }

    network_address_list_t list(job.hostname, job.port);
    auto cb = std::move(job.callback);
    auto list_sp =
        std::make_shared<network_address_list_t>(std::move(list));
    {
      std::lock_guard<std::mutex> lock(mutex_);
      completions_.push_back([cb = std::move(cb), list_sp]() mutable {
        network_address_list_t tmp = std::move(*list_sp);
        cb(std::move(tmp));
      });
    }
    if (eventfd_ >= 0) {
      uint64_t one = 1;
      if (write(eventfd_, &one, sizeof one) != sizeof one) {
        ERROR("dns_resolver: eventfd write: {}", strerror(errno));
      }
    }
  }
}

void dns_resolver_t::on_eventfd_readable(int) {
  uint64_t total = 0;
  for (;;) {
    uint64_t chunk = 0;
    ssize_t r = read(eventfd_, &chunk, sizeof chunk);
    if (r == sizeof chunk) {
      total += chunk;
      continue;
    }
    if (r < 0 && errno == EAGAIN) {
      break;
    }
    if (r < 0) {
      ERROR("dns_resolver: eventfd read: {}", strerror(errno));
    }
    break;
  }

  std::deque<std::function<void()>> batch;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint64_t i = 0; i < total && !completions_.empty(); ++i) {
      batch.push_back(std::move(completions_.front()));
      completions_.pop_front();
    }
  }

  for (auto &fn : batch) {
    if (fn) {
      fn();
    }
  }
}

void dns_resolver_t::resolve_async(std::string hostname, std::string port,
                                   std::function<void(network_address_list_t)>
                                       callback) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) {
      poller.call_later([cb = std::move(callback)]() mutable {
        cb(network_address_list_t());
      });
      return;
    }
  }
  ensure_started();
  if (eventfd_ < 0) {
    poller.call_later([cb = std::move(callback)]() mutable {
      cb(network_address_list_t());
    });
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) {
      poller.call_later([cb = std::move(callback)]() mutable {
        cb(network_address_list_t());
      });
      return;
    }
    pending_.push_back(
        job_t{std::move(hostname), std::move(port), std::move(callback)});
  }
  cv_.notify_one();
}

void dns_resolver_t::stop_worker_and_fd() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = true;
  }
  cv_.notify_all();
  if (worker_started_ && worker_.joinable()) {
    worker_.join();
  }
  worker_started_ = false;
  poll_listener_.stop();
  if (eventfd_ >= 0) {
    ::close(eventfd_);
    eventfd_ = -1;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = false;
  }
}

dns_resolver_t::~dns_resolver_t() {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    shutdown_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

dns_resolver_t &dns_resolver() {
  // Intentionally leaked so the destructor never runs during static teardown
  // (joinable std::thread would std::terminate; poller may be destroyed first).
  static dns_resolver_t *const instance = new dns_resolver_t();
  return *instance;
}

static std::mutex dns_shutdown_mutex;
static bool dns_shutdown_done = false;

void dns_resolver_shutdown() {
  std::lock_guard<std::mutex> lk(dns_shutdown_mutex);
  if (dns_shutdown_done) {
    return;
  }
  dns_shutdown_done = true;
  dns_resolver().stop_worker_and_fd();
}

} // namespace rtpmidid

/**
 * Low-priority periodic task runner.
 */
#include "cron_tasks.hpp"

#include "rtpmidid/logger.hpp"
#include "rtpmidid/shutdown_signals.hpp"

namespace rtpmididns {

cron_tasks_t::cron_tasks_t() = default;

cron_tasks_t::~cron_tasks_t() { stop(); }

void cron_tasks_t::start() {
  if (running_.exchange(true))
    return;
  thread_ = std::thread([this]() { thread_loop(); });
}

void cron_tasks_t::stop() {
  if (!running_.exchange(false))
    return;
  cv_.notify_all();
  if (thread_.joinable())
    thread_.join();
}

void cron_tasks_t::add_periodic(std::string name, std::chrono::seconds interval,
                                std::function<void()> fn) {
  periodic_task_t task;
  task.name = std::move(name);
  task.interval = interval;
  task.fn = std::move(fn);
  task.next_run = std::chrono::steady_clock::now() + interval;

  std::lock_guard<std::mutex> lock(mutex_);
  tasks_.push_back(std::move(task));
  cv_.notify_all();
}

void cron_tasks_t::thread_loop() {
  rtpmidid::block_shutdown_signals();
  INFO("cron_tasks: thread started");

  while (running_.load()) {
    std::vector<periodic_task_t> snapshot;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot = tasks_;
    }

    if (snapshot.empty()) {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::seconds(1),
                   [this]() { return !running_.load(); });
      continue;
    }

    const auto now = std::chrono::steady_clock::now();
    auto sleep_until = now + std::chrono::hours(24);
    for (auto &task : snapshot) {
      if (now >= task.next_run) {
        DEBUG("cron_tasks: running {}", task.name);
        try {
          task.fn();
        } catch (const std::exception &e) {
          ERROR("cron_tasks: {} failed: {}", task.name, e.what());
        }
        task.next_run = now + task.interval;
      }
      if (task.next_run < sleep_until)
        sleep_until = task.next_run;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (size_t i = 0; i < tasks_.size() && i < snapshot.size(); ++i)
        tasks_[i].next_run = snapshot[i].next_run;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_until(lock, sleep_until,
                   [this]() { return !running_.load(); });
  }

  INFO("cron_tasks: thread stopped");
}

} // namespace rtpmididns

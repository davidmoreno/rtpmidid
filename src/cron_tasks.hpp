/**
 * Low-priority periodic ("cron") tasks — not on the poller or MIDI hot paths.
 */
#pragma once

#include "utils.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rtpmididns {

class cron_tasks_t {
  NON_COPYABLE_NOR_MOVABLE(cron_tasks_t)

public:
  cron_tasks_t();
  ~cron_tasks_t();

  void start();
  void stop();

  void add_periodic(std::string name, std::chrono::seconds interval,
                    std::function<void()> fn);

private:
  struct periodic_task_t {
    std::string name;
    std::chrono::seconds interval{0};
    std::function<void()> fn;
    std::chrono::steady_clock::time_point next_run{};
  };

  std::vector<periodic_task_t> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread thread_;
  std::atomic<bool> running_{false};

  void thread_loop();
};

} // namespace rtpmididns

/// Worker and DNS messages (design D10/D13): blocking jobs and typed DNS
/// results. Lives next to `worker_actor.hpp`.

#pragma once

#include "message_core.hpp"
#include <functional>
#include <string>
#include <vector>

namespace rtpmididns {

/// A blocking job for the worker actor (C++23 `std::move_only_function`;
/// small closures stored inline; the worker never knows result types).
struct worker_job_t {
  std::move_only_function<void()> job;
};
/// Typed DNS resolution result posted back to the requester.
struct dns_resolved_t {
  hdr_t hdr;
  std::string hostname;
  std::string port;
  std::vector<std::string> addresses; // empty = resolution failed
};

/// The worker's accepted control messages: jobs only.
using worker_control_t = std::variant<stop_t, worker_job_t>;

} // namespace rtpmididns

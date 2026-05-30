/**
 * Shared time helpers.
 */
#include "time_utils.hpp"

#include <chrono>

namespace rtpmididns {

int64_t now_unix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

} // namespace rtpmididns

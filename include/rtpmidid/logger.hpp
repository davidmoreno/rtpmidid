/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once
#include "formatterhelper.hpp"
#include <array>
#include <atomic>
#include <iostream>
#include <string>

namespace rtpmidid {
enum logger_level_t { DEBUG, INFO, WARNING, ERROR };
}

ENUM_FORMATTER_BEGIN(rtpmidid::logger_level_t);
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::DEBUG, "DEBUG");
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::INFO, "INFO");
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::WARNING, "WARN");
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::ERROR, "ERROR");
ENUM_FORMATTER_END();

namespace rtpmidid {

/// A formatted log message, as handed to the logger actor: the level, the
/// source file (basename) and line number, and the already-formatted message
/// body. The producing thread substitutes the arguments into the body; the
/// sink never reformats, it only renders the final line.
struct log_message_t {
  logger_level_t level = logger_level_t::INFO;
  std::string file;
  int lineno = 0;
  std::string text;
  /// The producing thread's tag (e.g. the actor name), captured at
  /// production time as an owned copy: the producer and the renderer are
  /// different threads (the logger actor prints what others logged), and
  /// the producer may exit before the message is rendered, so a pointer
  /// into the producer's storage would dangle. Empty when the thread has
  /// no tag.
  std::string thread_name;
};

/// Per-thread log tag registry (one `thread_local` per thread, mirroring
/// the `thread_buffer()` pattern): any thread may set its tag (the actor
/// runtime does so at thread start), and `log()` captures it into every
/// emitted message. Threads that never set a tag produce untagged
/// messages, rendered exactly as before.
void set_log_thread_tag(std::string name);
const std::string &current_log_thread_tag();

/// Render a complete log line ("[INFO ] file.cpp:12 | message") with the
/// ANSI color decoration, exactly as the daemon printed before the logger
/// actor existed. Used by the direct-print fallback and by the daemon's
/// logger actor, so both paths produce byte-identical output.
std::string logger_format_line(const log_message_t &msg);

/// Routing hook installed by the daemon's logger actor (`install()`): once
/// set, every INFO/WARNING/DEBUG/ERROR hands its formatted message to the
/// actor, and the actor's single thread owns the stdout output. When unset
/// — lib-only programs, tests, early startup before the actor exists —
/// `log` prints directly, exactly as it always did.
extern void (*logger_log_sink)(log_message_t msg);

/// Process-global color enable flag, computed once at startup. When off,
/// `logger_format_line` skips tokenizing/coloring entirely.
void set_log_color_enabled(bool enabled);
bool log_color_enabled();

/// Compute the color-enabled decision from its inputs, in precedence order:
/// CLI --log-no-color > NO_COLOR > FORCE_COLOR > INI never/always > isatty.
/// Pure and testable; the daemon wires it to argv/env/INI/TTY at startup.
inline bool compute_log_color_enabled(bool cli_no_color, bool no_color_env,
                                      bool force_color_env, bool ini_never,
                                      bool ini_always, bool is_tty) {
  if (cli_no_color) {
    return false;
  }
  if (no_color_env) {
    return false;
  }
  if (force_color_env) {
    return true;
  }
  if (ini_never) {
    return false;
  }
  if (ini_always) {
    return true;
  }
  return is_tty;
}

/// Lowercase logfmt level name: debug/info/warning/error.
inline const char *log_level_name(logger_level_t level) {
  switch (level) {
  case logger_level_t::DEBUG:
    return "debug";
  case logger_level_t::INFO:
    return "info";
  case logger_level_t::WARNING:
    return "warning";
  case logger_level_t::ERROR:
    return "error";
  default:
    return "unknown";
  }
}

/// Basename of a source file path for the file= log field.
inline std::string log_basename(const char *filename) {
  const char *base = filename;
  for (const char *p = filename; *p != '\0'; ++p) {
    if (*p == '/') {
      base = p + 1;
    }
  }
  return std::string(base);
}

// One-line rendering for mailbox drop diagnostics.
inline std::string to_string(const log_message_t &m) {
  return "log{level=" + std::to_string(static_cast<int>(m.level)) +
         ", file=\"" + m.file + "\", lineno=" + std::to_string(m.lineno) +
         ", text=\"" + m.text + "\"}";
}

class logger_t {
public:
  void set_log_level(logger_level_t level) {
    current_log_level.store(level, std::memory_order_relaxed);
  }
  logger_level_t log_level() const {
    return current_log_level.load(std::memory_order_relaxed);
  }

  template <typename... Args>
  void log(logger_level_t level, const char *filename, int lineno,
           FMT::format_string<Args...> message, Args... args) {
    // Runtime filtering: only log if level is at or above current_log_level.
    if (level < log_level()) {
      return;
    }

    // Format the message body on the producing thread (the "already
    // formatted char list" the logger actor receives). The per-thread
    // buffer keeps logging race-free: the old shared buffer interleaved
    // when two threads logged concurrently.
    auto &buffer = thread_buffer();
    const auto res = FMT::format_to_n(buffer.begin(), buffer.size() - 16,
                                      message, std::forward<Args>(args)...);

    log_message_t msg{level, log_basename(filename), lineno,
                      std::string(buffer.begin(), res.out)};
    // Capture the producing thread's tag (owned copy, see log_message_t).
    msg.thread_name = current_log_thread_tag();
    if (logger_log_sink != nullptr) {
      logger_log_sink(std::move(msg));
    } else {
      // No logger actor installed (lib-only program, test, early startup):
      // keep the original direct print so output is unchanged.
      std::cout << logger_format_line(msg) << std::endl;
    }
  }

private:
  /// One preallocated buffer per thread, shared by all `log` instantiations
  /// (we never allocate on the debug path).
  static std::array<char, 1024> &thread_buffer() {
    static thread_local std::array<char, 1024> buffer;
    return buffer;
  }

  std::atomic<logger_level_t> current_log_level{logger_level_t::INFO};
};
} // namespace rtpmidid

namespace rtpmidid {
extern rtpmidid::logger_t logger2;

// Convert string to logger level (case-insensitive, accepts "debug"/"info"/"warning"/"error" or "0"/"1"/"2"/"3")
logger_level_t str_to_log_level(const std::string &value);
};

/// Compatibility with c++23, std::print, std::println
#if __cplusplus < 202302L
namespace std {
template <typename... Args>
void print(FMT::format_string<Args...> format, Args... args) {
  std::cout << FMT::format(format, std::forward<Args>(args)...);
}

template <typename... Args>
void println(FMT::format_string<Args...> format, Args... args) {
  std::cout << FMT::format(format, std::forward<Args>(args)...) << std::endl;
}
} // namespace std
#endif

#ifdef DEBUG
#undef DEBUG
#endif
#ifdef INFO
#undef INFO
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef WARNING
#undef WARNING
#endif

#ifndef LOG_LEVEL
#define LOG_LEVEL 1 // 1: debug, 2: info, 3: warning, 4: error
#endif

#if LOG_LEVEL <= 1
#define DEBUG(...)                                                             \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::DEBUG, __FILE__, __LINE__, \
                          __VA_ARGS__)
#else
#define DEBUG(...)
#endif
#if LOG_LEVEL <= 2
#define INFO(...)                                                              \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::INFO, __FILE__, __LINE__,  \
                          __VA_ARGS__)
#else
#define INFO(...)
#endif
#if LOG_LEVEL <= 3
#define WARNING(...)                                                           \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::WARNING, __FILE__,         \
                          __LINE__, __VA_ARGS__)
#else
#define WARNING(...)
#endif
#if LOG_LEVEL <= 4
#define ERROR(...)                                                             \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::ERROR, __FILE__, __LINE__, \
                          __VA_ARGS__)
#else
#define ERROR(...)
#endif

#define WARNING_RATE_LIMIT(seconds, ...)                                       \
  {                                                                            \
    static int __warning_skip_until_##__LINENO__ = 0;                          \
    int __now = time(nullptr);                                                 \
    if (__warning_skip_until_##__LINENO__ < __now) {                           \
      __warning_skip_until_##__LINENO__ = __now + seconds;                     \
      WARNING(__VA_ARGS__);                                                    \
    }                                                                          \
  }

#define ERROR_ONCE(...)                                                        \
  {                                                                            \
    static bool __error_once_unseen_##__LINENO__ = true;                       \
    if (__error_once_unseen_##__LINENO__) {                                    \
      __error_once_unseen_##__LINENO__ = false;                                \
      ERROR(__VA_ARGS__);                                                      \
    }                                                                          \
  }

#define WARNING_ONCE(...)                                                      \
  {                                                                            \
    static bool __warning_once_unseen_##__LINENO__ = true;                     \
    if (__warning_once_unseen_##__LINENO__) {                                  \
      __warning_once_unseen_##__LINENO__ = false;                              \
      WARNING(__VA_ARGS__);                                                    \
    }                                                                          \
  }

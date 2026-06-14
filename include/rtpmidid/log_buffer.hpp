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

#include <rtpmidid/signal.hpp>
#include <atomic>
#include <cstdint>
#include <map>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace rtpmidid {

/**
 * A single buffered log entry.
 *
 * Tags are embedded in `message` as logfmt `key=value` pairs — no
 * separate tag fields. They are extracted at query time by
 * parse_logfmt_tags().
 */
struct log_entry_t {
  uint64_t seq = 0;            // monotonic sequence number
  uint64_t timestamp_us = 0;   // steady_clock time_point at emission
  int level = 0;               // 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR
  std::string file;            // basename only, e.g. "midirouter.cpp"
  int line = 0;
  std::string message;         // logfmt body with inline tags
};

/**
 * A parsed LogQL-style query.
 *
 * Grammar:
 *   q = stream_selector (pipe_stage)*
 *   stream_selector = "{" key "=" value ("," key "=" value)* "}"
 *                   | ""
 *   pipe_stage = "|=" value   (case-insensitive contains)
 *              | "!=" value   (negated contains)
 */
struct logql_query_t {
  struct stage_t {
    enum op_t { CONTAINS, NOT_CONTAINS };
    op_t op;
    std::string value;
  };

  /// Stream selector: key → value (AND-ed). Empty = match all entries.
  std::map<std::string, std::string> tags;

  /// Pipeline stages applied to the raw message string, in order.
  std::vector<stage_t> pipeline;
};

/**
 * Parse a logfmt line into a tag map.
 *
 * Scans leading `key=value` and `key="quoted value"` pairs.
 * Stops at the first word that is not a key=value pair.
 *
 * Example:
 *   "peer_id=5 component=router Added peer" →
 *     {"peer_id": "5", "component": "router"}
 */
std::map<std::string, std::string> parse_logfmt_tags(std::string_view message);

/**
 * Parse a LogQL-style query string.
 *
 * Example:
 *   "{peer_id=\"5\", component=\"router\"} |= \"timeout\""
 *
 * The stream selector {} contains tag filters (AND-ed).
 * Each |= or != adds a pipeline stage (case-insensitive substring on raw message).
 * An empty string or "" returns a match-all query.
 */
logql_query_t parse_logql(std::string_view q);

/**
 * Thread-safe ring buffer of log_entry_t.
 *
 * Writes are lock-free via atomic write_pos_ / seq_.
 * Reads use a shared_mutex so concurrent queries don't block each other.
 */
class log_buffer_t {
public:
  explicit log_buffer_t(size_t capacity = 1024);

  /** Push a log entry (called from logger thread only). */
  void push(log_entry_t entry);

  /**
   * Resize the ring buffer (clears all existing entries).
   * Only safe to call before any concurrent pushes.
   */
  void resize(size_t new_capacity);

  /**
   * Query the ring buffer.
   *
   * @param q       LogQL query string (empty = return all).
   * @param since_seq Only return entries with seq > this.
   * @param since_us  Only return entries newer than this timestamp.
   * @param limit     Max entries to return (clamped to capacity).
   */
  std::vector<log_entry_t> query(std::string_view q,
                                  uint64_t since_seq = 0,
                                  uint64_t since_us = 0,
                                  int limit = 100) const;

  /// Total capacity of the ring buffer.
  size_t capacity() const { return capacity_; }

  /// Oldest sequence number still in the buffer (0 if empty).
  uint64_t oldest_seq() const;

  /// Newest sequence number in the buffer (0 if empty).
  uint64_t newest_seq() const;

private:
  std::vector<log_entry_t> ring_;
  std::atomic<uint64_t> write_pos_{0};
  /// Monotonic sequence counter, starts at 1 (0 = empty slot).
  std::atomic<uint64_t> seq_{1};
  size_t capacity_;
  mutable std::shared_mutex mutex_;

public:
  /// Fired on every push() from the logger thread.
  /// Subscribers: WebSocket event forwarding.
  signal_t<const log_entry_t &> on_new_entry;
};

/// Global ring buffer instance (defined in lib/logger.cpp).
extern log_buffer_t g_log_buffer;

} // namespace rtpmidid

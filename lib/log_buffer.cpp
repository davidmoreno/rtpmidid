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

#include <rtpmidid/log_buffer.hpp>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <mutex>

namespace rtpmidid {

// ─────────────────────────────────────────────────────────────────────────────
// logfmt parser
// ─────────────────────────────────────────────────────────────────────────────

std::map<std::string, std::string> parse_logfmt_tags(std::string_view message) {
  std::map<std::string, std::string> tags;
  const char *p = message.data();
  const char *end = p + message.size();

  while (p < end) {
    // Skip whitespace between tags
    while (p < end && std::isspace(static_cast<unsigned char>(*p)))
      ++p;
    if (p >= end)
      break;

    // Read key: [a-z_][a-z0-9_]*
    const char *key_start = p;
    if (!std::isalpha(static_cast<unsigned char>(*p)) && *p != '_')
      break; // not a tag — stop
    ++p;
    while (p < end && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
      ++p;
    if (p >= end || *p != '=')
      break; // not a key=value pair — stop
    std::string_view key(key_start, p - key_start);
    ++p; // skip '='

    if (p >= end)
      break;

    // Read value
    std::string value;
    if (*p == '"') {
      // Quoted value
      ++p; // skip opening quote
      while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) {
          ++p;
          value += *p;
        } else {
          value += *p;
        }
        ++p;
      }
      if (p < end)
        ++p; // skip closing quote
    } else {
      // Bare value — until space or end
      const char *val_start = p;
      while (p < end && !std::isspace(static_cast<unsigned char>(*p)))
        ++p;
      value.assign(val_start, p - val_start);
    }

    if (!key.empty())
      tags[std::string(key)] = std::move(value);
  }

  return tags;
}

// ─────────────────────────────────────────────────────────────────────────────
// LogQL parser
// ─────────────────────────────────────────────────────────────────────────────

// Helper: skip whitespace
static void skip_ws(const char *&p, const char *end) {
  while (p < end && std::isspace(static_cast<unsigned char>(*p)))
    ++p;
}

// Helper: read a quoted or bare value
static std::string read_value(const char *&p, const char *end) {
  skip_ws(p, end);
  if (p >= end)
    return {};
  if (*p == '"') {
    ++p; // skip opening quote
    std::string val;
    while (p < end && *p != '"') {
      if (*p == '\\' && p + 1 < end) {
        ++p;
        val += *p;
      } else {
        val += *p;
      }
      ++p;
    }
    if (p < end)
      ++p; // skip closing quote
    return val;
  }
  // Bare value — until space, comma, }, |, or end
  const char *start = p;
  while (p < end && *p != ' ' && *p != ',' && *p != '}' && *p != '|')
    ++p;
  return {start, p};
}

logql_query_t parse_logql(std::string_view q) {
  logql_query_t query;

  if (q.empty())
    return query;

  const char *p = q.data();
  const char *end = p + q.size();

  skip_ws(p, end);

  // ── Stream selector: {key="value", ...} ──────────────────────────────
  if (p < end && *p == '{') {
    ++p; // skip '{'
    skip_ws(p, end);

    while (p < end && *p != '}') {
      // Read key
      const char *key_start = p;
      while (p < end && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
        ++p;
      std::string key(key_start, p - key_start);

      skip_ws(p, end);
      if (p >= end || *p != '=') {
        // Malformed — stop selector parsing
        query.tags.clear();
        break;
      }
      ++p; // skip '='

      std::string value = read_value(p, end);
      if (!key.empty())
        query.tags[std::move(key)] = std::move(value);

      skip_ws(p, end);
      if (p < end && *p == ',')
        ++p; // skip ','
      skip_ws(p, end);
    }
    if (p < end && *p == '}')
      ++p; // skip '}'
  }

  // ── Pipeline stages: |= "value", != "value", ... ────────────────────
  while (p < end) {
    skip_ws(p, end);
    if (p >= end)
      break;

    // Expect "|=" or "!="
    if (p + 1 >= end)
      break;

    logql_query_t::stage_t::op_t op;
    if (*p == '|' && *(p + 1) == '=') {
      op = logql_query_t::stage_t::CONTAINS;
      p += 2;
    } else if (*p == '!' && *(p + 1) == '=') {
      op = logql_query_t::stage_t::NOT_CONTAINS;
      p += 2;
    } else if (*p == '|' && *(p + 1) == '~') {
      // Regex — not implemented yet, skip to next stage
      p += 2;
      read_value(p, end); // consume value
      continue;
    } else if (*p == '!' && *(p + 1) == '~') {
      p += 2;
      read_value(p, end); // consume value
      continue;
    } else {
      // Unknown token — stop parsing
      break;
    }

    std::string value = read_value(p, end);
    if (!value.empty()) {
      query.pipeline.push_back({op, std::move(value)});
    }
  }

  return query;
}

// ─────────────────────────────────────────────────────────────────────────────
// log_buffer_t
// ─────────────────────────────────────────────────────────────────────────────

log_buffer_t::log_buffer_t(size_t capacity)
    : ring_(capacity), capacity_(capacity) {
  assert(capacity >= 1);
}

void log_buffer_t::push(log_entry_t entry) {
  const uint64_t pos = write_pos_.fetch_add(1, std::memory_order_relaxed);
  const size_t idx = static_cast<size_t>(pos % capacity_);

  entry.seq = seq_.fetch_add(1, std::memory_order_relaxed);

  ring_[idx] = std::move(entry);
}

void log_buffer_t::resize(size_t new_capacity) {
  assert(new_capacity >= 1);
  std::unique_lock lock(mutex_);
  ring_.assign(new_capacity, log_entry_t{});
  capacity_ = new_capacity;
  write_pos_.store(0, std::memory_order_relaxed);
  seq_.store(1, std::memory_order_relaxed);
}

uint64_t log_buffer_t::oldest_seq() const {
  std::shared_lock lock(mutex_);
  const uint64_t pos = write_pos_.load(std::memory_order_acquire);
  const uint64_t seq = seq_.load(std::memory_order_acquire);
  // seq starts at 1; 0 means no entries pushed yet
  if (seq <= 1)
    return 0;
  // If buffer hasn't wrapped yet, oldest is seq 1.
  // After wrapping, oldest is seq - capacity.
  if (pos < capacity_)
    return 1;
  return seq - capacity_;
}

uint64_t log_buffer_t::newest_seq() const {
  const uint64_t seq = seq_.load(std::memory_order_acquire);
  // seq_ is the NEXT sequence number to be assigned
  return seq > 1 ? seq - 1 : 0;
}

// Helper: case-insensitive substring match
static bool icontains(std::string_view haystack, std::string_view needle) {
  if (needle.empty())
    return true;
  if (haystack.size() < needle.size())
    return false;
  return std::search(
             haystack.begin(), haystack.end(), needle.begin(), needle.end(),
             [](unsigned char a, unsigned char b) {
               return std::tolower(a) == std::tolower(b);
             }) != haystack.end();
}

std::vector<log_entry_t> log_buffer_t::query(
    std::string_view q, uint64_t since_seq, uint64_t since_us,
    int limit) const {

  if (limit <= 0)
    return {};
  if (static_cast<size_t>(limit) > capacity_)
    limit = static_cast<int>(capacity_);

  // Parse the query once
  logql_query_t parsed = parse_logql(q);

  std::shared_lock lock(mutex_);

  const uint64_t wr = write_pos_.load(std::memory_order_acquire);

  std::vector<log_entry_t> results;
  results.reserve(static_cast<size_t>(limit));

  // Walk backwards from newest to oldest
  uint64_t start = wr;
  uint64_t scanned = 0;
  const uint64_t total = std::min(capacity_, wr);

  while (scanned < total && static_cast<int>(results.size()) < limit) {
    if (start == 0)
      start = capacity_;
    --start;
    ++scanned;

    const log_entry_t &entry = ring_[start % capacity_];

    // Skip entries not yet written (seq == 0 on first wrap)
    if (entry.seq == 0)
      continue;

    // since_seq filter
    if (since_seq > 0 && entry.seq <= since_seq)
      continue;

    // since_us filter
    if (since_us > 0 && entry.timestamp_us <= since_us)
      continue;

    // ── Stream selector filter ──────────────────────────────────────
    if (!parsed.tags.empty()) {
      auto tags = parse_logfmt_tags(entry.message);
      bool match = true;
      for (const auto &[key, val] : parsed.tags) {
        auto it = tags.find(key);
        if (it == tags.end() || it->second != val) {
          match = false;
          break;
        }
      }
      if (!match)
        continue;
    }

    // ── Pipeline filters ────────────────────────────────────────────
    bool pipeline_ok = true;
    for (const auto &stage : parsed.pipeline) {
      const bool contains = icontains(entry.message, stage.value);
      if (stage.op == logql_query_t::stage_t::CONTAINS) {
        if (!contains) {
          pipeline_ok = false;
          break;
        }
      } else { // NOT_CONTAINS
        if (contains) {
          pipeline_ok = false;
          break;
        }
      }
    }
    if (!pipeline_ok)
      continue;

    results.push_back(entry);
  }

  // Results are in reverse chronological order — reverse to chronological
  std::reverse(results.begin(), results.end());

  return results;
}

} // namespace rtpmidid

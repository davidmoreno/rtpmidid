/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <rtpmidid/log_buffer.hpp>
#include <rtpmidid/logger.hpp>
#include "test_case.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace rtpmidid;

// ─────────────────────────────────────────────────────────────────────────────
// parse_logfmt_tags
// ─────────────────────────────────────────────────────────────────────────────

static void test_logfmt_tags_simple() {
  auto tags = parse_logfmt_tags("peer_id=5 component=router Added peer");
  ASSERT_EQUAL(tags.size(), 2);
  ASSERT_EQUAL(tags["peer_id"], "5");
  ASSERT_EQUAL(tags["component"], "router");
}

static void test_logfmt_tags_quoted() {
  auto tags = parse_logfmt_tags(
      "connection_id=\"alsa_seq:client=20,port=0\" msg=\"hello world\" rest");
  ASSERT_EQUAL(tags.size(), 2);
  ASSERT_EQUAL(tags["connection_id"], "alsa_seq:client=20,port=0");
  ASSERT_EQUAL(tags["msg"], "hello world");
}

static void test_logfmt_tags_escaped_quote() {
  auto tags = parse_logfmt_tags("msg=\"hello \\\"world\\\"\" tail");
  ASSERT_EQUAL(tags["msg"], "hello \"world\"");
}

static void test_logfmt_tags_empty() {
  auto tags = parse_logfmt_tags("");
  ASSERT_TRUE(tags.empty());
}

static void test_logfmt_tags_no_tags() {
  auto tags = parse_logfmt_tags("Just a plain message without tags");
  ASSERT_TRUE(tags.empty());
}

static void test_logfmt_tags_stops_at_non_tag() {
  // "timeout" is not a tag (no =), so parsing stops
  auto tags = parse_logfmt_tags("peer_id=5 timeout connection_refused");
  ASSERT_EQUAL(tags.size(), 1);
  ASSERT_EQUAL(tags["peer_id"], "5");
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_logql
// ─────────────────────────────────────────────────────────────────────────────

static void test_logql_empty() {
  auto q = parse_logql("");
  ASSERT_TRUE(q.tags.empty());
  ASSERT_TRUE(q.pipeline.empty());

  q = parse_logql("\"\"");
  ASSERT_TRUE(q.tags.empty());
  ASSERT_TRUE(q.pipeline.empty());
}

static void test_logql_stream_selector() {
  auto q = parse_logql("{peer_id=\"5\", component=\"router\"}");
  ASSERT_EQUAL(q.tags.size(), 2);
  ASSERT_EQUAL(q.tags["peer_id"], "5");
  ASSERT_EQUAL(q.tags["component"], "router");
  ASSERT_TRUE(q.pipeline.empty());
}

static void test_logql_stream_no_quotes() {
  auto q = parse_logql("{peer_id=5, component=router}");
  ASSERT_EQUAL(q.tags.size(), 2);
  ASSERT_EQUAL(q.tags["peer_id"], "5");
  ASSERT_EQUAL(q.tags["component"], "router");
}

static void test_logql_single_tag() {
  auto q = parse_logql("{level=\"error\"}");
  ASSERT_EQUAL(q.tags.size(), 1);
  ASSERT_EQUAL(q.tags["level"], "error");
}

static void test_logql_stream_with_contains() {
  auto q = parse_logql("{peer_id=\"5\"} |= \"timeout\"");
  ASSERT_EQUAL(q.tags.size(), 1);
  ASSERT_EQUAL(q.tags["peer_id"], "5");
  ASSERT_EQUAL(q.pipeline.size(), 1);
  ASSERT_TRUE(q.pipeline[0].op == logql_query_t::stage_t::CONTAINS);
  ASSERT_EQUAL(q.pipeline[0].value, "timeout");
}

static void test_logql_stream_with_not_contains() {
  auto q = parse_logql("{component=\"rtp\"} != \"debug\"");
  ASSERT_EQUAL(q.tags.size(), 1);
  ASSERT_EQUAL(q.pipeline.size(), 1);
  ASSERT_TRUE(q.pipeline[0].op == logql_query_t::stage_t::NOT_CONTAINS);
  ASSERT_EQUAL(q.pipeline[0].value, "debug");
}

static void test_logql_multiple_pipeline() {
  auto q = parse_logql("{} |= \"synth\" |= \"confirmation\"");
  ASSERT_TRUE(q.tags.empty());
  ASSERT_EQUAL(q.pipeline.size(), 2);
  ASSERT_EQUAL(q.pipeline[0].value, "synth");
  ASSERT_EQUAL(q.pipeline[1].value, "confirmation");
}

static void test_logql_only_contains() {
  auto q = parse_logql("|= \"timeout\"");
  ASSERT_TRUE(q.tags.empty());
  ASSERT_EQUAL(q.pipeline.size(), 1);
  ASSERT_EQUAL(q.pipeline[0].value, "timeout");
}

static void test_logql_quoted_value_with_spaces() {
  auto q = parse_logql("{msg=\"connection refused\"}");
  ASSERT_EQUAL(q.tags.size(), 1);
  ASSERT_EQUAL(q.tags["msg"], "connection refused");
}

static void test_logql_mixed_quoted_bare() {
  auto q = parse_logql("{peer_id=5, msg=\"hello world\"}");
  ASSERT_EQUAL(q.tags.size(), 2);
  ASSERT_EQUAL(q.tags["peer_id"], "5");
  ASSERT_EQUAL(q.tags["msg"], "hello world");
}

// ─────────────────────────────────────────────────────────────────────────────
// log_buffer_t push / query
// ─────────────────────────────────────────────────────────────────────────────

static void test_buffer_push_and_query() {
  log_buffer_t buf(16);

  // Empty buffer
  auto results = buf.query("", 0, 0, 100);
  ASSERT_TRUE(results.empty());
  ASSERT_EQUAL(buf.oldest_seq(), 0);
  ASSERT_EQUAL(buf.newest_seq(), 0);

  // Push entries with tags
  for (int i = 0; i < 5; ++i) {
    log_entry_t e;
    e.timestamp_us = 1000 + static_cast<uint64_t>(i);
    e.level = 1;
    e.file = "test.cpp";
    e.line = i + 1;
    e.message = "peer_id=" + std::to_string(i / 2) + " component=router msg "
                + std::to_string(i);
    buf.push(std::move(e));
  }

  ASSERT_EQUAL(buf.oldest_seq(), 1);
  ASSERT_EQUAL(buf.newest_seq(), 5);

  // Query all
  results = buf.query("", 0, 0, 100);
  ASSERT_EQUAL(results.size(), 5);

  // Query with limit
  results = buf.query("", 0, 0, 2);
  ASSERT_EQUAL(results.size(), 2);
  ASSERT_EQUAL(results[0].seq, 4); // newest 2 entries
  ASSERT_EQUAL(results[1].seq, 5);

  // since_seq
  results = buf.query("", 2, 0, 100);
  ASSERT_EQUAL(results.size(), 3);
  ASSERT_EQUAL(results[0].seq, 3);

  // Stream filter
  results = buf.query("{peer_id=\"0\"}", 0, 0, 100);
  ASSERT_EQUAL(results.size(), 2);
  ASSERT_EQUAL(results[0].seq, 1);
  ASSERT_EQUAL(results[1].seq, 2);

  // Contains filter
  results = buf.query("{peer_id=\"1\"} |= \"msg 2\"", 0, 0, 100);
  ASSERT_EQUAL(results.size(), 1);
  ASSERT_EQUAL(results[0].seq, 3);

  // NOT contains filter
  results = buf.query("{} != \"msg 2\"", 0, 0, 100);
  ASSERT_EQUAL(results.size(), 4);
}

static void test_buffer_wrap_around() {
  log_buffer_t buf(4);

  // Fill with 6 entries — should wrap
  for (int i = 0; i < 6; ++i) {
    log_entry_t e;
    e.timestamp_us = static_cast<uint64_t>(i);
    e.level = 1;
    e.file = "x.cpp";
    e.line = i;
    e.message = "msg " + std::to_string(i);
    buf.push(std::move(e));
  }

  ASSERT_EQUAL(buf.capacity(), 4);
  ASSERT_EQUAL(buf.oldest_seq(), 3);
  ASSERT_EQUAL(buf.newest_seq(), 6);

  // Should see last 4 entries (seq 3, 4, 5, 6)
  auto results = buf.query("", 0, 0, 100);
  ASSERT_EQUAL(results.size(), 4);
  ASSERT_EQUAL(results[0].seq, 3);
  ASSERT_EQUAL(results[3].seq, 6);

  // since_seq should skip wrapped-over entries
  results = buf.query("", 3, 0, 100);
  ASSERT_EQUAL(results.size(), 3);
  ASSERT_EQUAL(results[0].seq, 4);
  ASSERT_EQUAL(results[1].seq, 5);
  ASSERT_EQUAL(results[2].seq, 6);
}

static void test_buffer_pipeline_order() {
  log_buffer_t buf(8);

  log_entry_t e;
  e.timestamp_us = 1;
  e.level = 1;
  e.file = "t.cpp";
  e.line = 1;

  e.message = "peer_id=1 component=rtp Got confirmation from synth.local";
  buf.push(e);

  e.timestamp_us = 2;
  e.message = "peer_id=2 component=rtp Got confirmation from piano.local";
  buf.push(e);

  e.timestamp_us = 3;
  e.message = "peer_id=1 component=rtp Disconnect from synth.local";
  buf.push(e);

  // Filter: peer 1, contains "confirmation", NOT "piano"
  auto results = buf.query(
      "{peer_id=\"1\"} |= \"confirmation\" != \"piano\"", 0, 0, 100);
  ASSERT_EQUAL(results.size(), 1);
  ASSERT_EQUAL(results[0].seq, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_logfmt_tags on real logger messages
// ─────────────────────────────────────────────────────────────────────────────

static void test_logfmt_from_logger() {
  // Simulate what the logger produces (plain-text, no ANSI)
  std::string msg = "midirouter.cpp:164 peer_id=5 component=router "
                    "Added peer type=rtpmidi_client peer_id=5";

  auto tags = parse_logfmt_tags(msg);
  // "midirouter.cpp:164" has no '=' → not a tag → stops before it
  // So tags should be empty since the first word isn't a key=value
  ASSERT_TRUE(tags.empty());
  // This is expected — file:line prefix comes before tags in logger output.
  // The LogQL stream selector won't match file/line from this format,
  // but it WILL match explicit tags placed after the preamble.
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
  test_logfmt_tags_simple();
  test_logfmt_tags_quoted();
  test_logfmt_tags_escaped_quote();
  test_logfmt_tags_empty();
  test_logfmt_tags_no_tags();
  test_logfmt_tags_stops_at_non_tag();

  test_logql_empty();
  test_logql_stream_selector();
  test_logql_stream_no_quotes();
  test_logql_single_tag();
  test_logql_stream_with_contains();
  test_logql_stream_with_not_contains();
  test_logql_multiple_pipeline();
  test_logql_only_contains();
  test_logql_quoted_value_with_spaces();
  test_logql_mixed_quoted_bare();

  test_buffer_push_and_query();
  test_buffer_wrap_around();
  test_buffer_pipeline_order();

  test_logfmt_from_logger();

  std::cout << "All log_buffer tests passed." << std::endl;
  return 0;
}

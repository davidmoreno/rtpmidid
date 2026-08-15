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

/// Logger actor tests (threadless pump mode): logfmt line rendering, the
/// `quoted_t` value helper, color highlighting and its configuration, macro
/// routing through the installed sink, producer-side level filtering, the
/// stop-drains-the-queue guarantee, the direct-print fallback when no actor
/// is installed, and the per-thread log tag.

#include "logger_actor.hpp"
#include "test_case.hpp"
#include "test_utils.hpp"
#include <atomic>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace rtpmididns;

/// Redirect std::cout for the duration of the scope (the logger actor
/// renders through std::cout, exactly like the old direct print).
class cout_capture_t {
public:
  cout_capture_t() : old_(std::cout.rdbuf(stream_.rdbuf())) {}
  ~cout_capture_t() { std::cout.rdbuf(old_); }
  std::string str() const { return stream_.str(); }

private:
  std::stringstream stream_;
  std::streambuf *old_;
};

/// Install the logger sink for the duration of the scope; on failure the
/// harness messages keep printing directly instead of vanishing into a
/// stopped test actor.
class sink_guard_t {
public:
  explicit sink_guard_t(logger_actor_t &logger) { logger.install(); }
  ~sink_guard_t() { rtpmidid::logger_log_sink = nullptr; }
};

static void test_actor_formats_and_prints() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  cout_capture_t capture;
  rtpmidid::set_log_color_enabled(false);

  logger.mailbox()->post_data(rtpmidid::log_message_t{
      rtpmidid::logger_level_t::INFO, "myfile.cpp", 42, "hello world"});
  logger.pump();

  const std::string expected =
      std::string("level=info thread= filename=myfile.cpp:42") +
      std::string(23, ' ') + " hello world\n";
  ASSERT_EQUAL(capture.str(), expected);
}

static void test_macros_route_through_sink() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  sink_guard_t guard(logger);
  {
    rtpmidid::set_log_color_enabled(false);
    cout_capture_t capture;
    INFO("routed value={}", 42);
    WARNING("careful value={}", "now");
    logger.pump();
    ASSERT_TRUE(capture.str().find("routed value=42") != std::string::npos);
    ASSERT_TRUE(capture.str().find("level=info") != std::string::npos);
    ASSERT_TRUE(capture.str().find("level=warning") != std::string::npos);
    ASSERT_TRUE(capture.str().find("careful value=now") != std::string::npos);
  }
}

static void test_producer_level_filter() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  sink_guard_t guard(logger);
  rtpmidid::set_log_color_enabled(false);

  rtpmidid::logger2.set_log_level(rtpmidid::logger_level_t::ERROR);
  cout_capture_t capture;
  INFO("filtered below ERROR value={}", 1);
  logger.pump();
  // The producer filtered the message: nothing was posted, nothing printed.
  ASSERT_TRUE(capture.str().empty());

  rtpmidid::logger2.set_log_level(rtpmidid::logger_level_t::INFO);
  cout_capture_t capture2;
  ERROR("still routed value={}", 2);
  logger.pump();
  ASSERT_TRUE(capture2.str().find("still routed value=2") != std::string::npos);
  rtpmidid::logger2.set_log_level(rtpmidid::logger_level_t::INFO);
}

static void test_stop_drains_queue() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  cout_capture_t capture;
  rtpmidid::set_log_color_enabled(false);

  for (int i = 0; i < 5; i++) {
    logger.mailbox()->post_data(rtpmidid::log_message_t{
        rtpmidid::logger_level_t::INFO, "flood.cpp", 1,
        "line " + std::to_string(i)});
  }
  logger.request_stop(); // the stop control message

  int guard = 0;
  while (logger.pump() && guard++ < 10000) {
  }
  ASSERT_LT(guard, 10000); // the loop must exit (stop processed)

  // Every queued line was drained and printed before the loop exited.
  const auto out = capture.str();
  for (int i = 0; i < 5; i++) {
    ASSERT_TRUE(out.find("line " + std::to_string(i)) != std::string::npos);
  }

  // Posting to a stopped logger stays safe (the mailbox outlives it).
  logger.mailbox()->post_data(rtpmidid::log_message_t{
      rtpmidid::logger_level_t::INFO, "late.cpp", 1, "after stop"});
}

static void test_no_sink_prints_directly() {
  rtpmidid::logger_log_sink = nullptr; // no actor installed
  rtpmidid::set_log_color_enabled(false);
  cout_capture_t capture;
  INFO("direct value={}", 7);
  ASSERT_TRUE(capture.str().find("direct value=7") != std::string::npos);
}

/// Tagged messages render the tag as the `thread=` field.
static void test_thread_tag_rendering() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  cout_capture_t capture;
  rtpmidid::set_log_color_enabled(false);

  logger.mailbox()->post_data(rtpmidid::log_message_t{
      rtpmidid::logger_level_t::INFO, "router_actor.cpp", 12, "peer up",
      "router"});
  logger.pump();

  const std::string expected =
      std::string("level=info thread=router filename=router_actor.cpp:12") +
      std::string(11, ' ') + " peer up\n";
  ASSERT_EQUAL(capture.str(), expected);
}

/// The thread tag is captured at production time and renders as `thread=`.
static void test_thread_tag_capture_and_legacy() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  sink_guard_t guard(logger);
  rtpmidid::set_log_color_enabled(false);
  {
    rtpmidid::set_log_thread_tag("worker");
    cout_capture_t capture;
    INFO("tagged from worker");
    logger.pump();
    ASSERT_TRUE(capture.str().find("thread=worker") != std::string::npos);
    ASSERT_TRUE(capture.str().find("tagged from worker") !=
                std::string::npos);
  }
  {
    rtpmidid::set_log_thread_tag(""); // untagged: empty thread= field
    cout_capture_t capture;
    INFO("untagged message");
    logger.pump();
    ASSERT_TRUE(capture.str().find("thread= filename=") != std::string::npos);
    ASSERT_TRUE(capture.str().find("untagged message") != std::string::npos);
  }
}

/// The tag is an owned copy, so it survives the producing thread exiting
/// before the logger renders the message — no dangling reference.
static void test_tag_survives_producer_exit() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  sink_guard_t guard(logger);
  rtpmidid::set_log_color_enabled(false);
  cout_capture_t capture;

  std::thread producer([] {
    rtpmidid::set_log_thread_tag("ephemeral");
    INFO("from a dying thread");
  });
  producer.join();
  logger.pump();

  ASSERT_TRUE(capture.str().find("thread=ephemeral") != std::string::npos);
  ASSERT_TRUE(capture.str().find("from a dying thread") != std::string::npos);
}

/// Real-thread hammering of the drain-on-stop guarantee: a producer thread
/// posts continuously while the main thread stops the actor, so the stop
/// races the in-progress drain.
static void test_threaded_stop_drains_racing_producer() {
  for (int round = 0; round < 10; round++) {
    auto supervisor = std::make_shared<test_mailbox_t>();
    auto logger = std::make_shared<logger_actor_t>(
        actor_config_t{.name = "logger", .supervisor_mailbox = supervisor});
    cout_capture_t capture; // silence the printed lines
    logger->start();

    std::atomic<bool> keep_going{true};
    std::thread producer([&] {
      uint64_t i = 0;
      while (keep_going.load(std::memory_order_relaxed)) {
        logger->mailbox()->post_data(rtpmidid::log_message_t{
            rtpmidid::logger_level_t::INFO, "race.cpp", 1,
            "msg " + std::to_string(i++)});
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    logger->request_stop(); // races the in-progress drain
    keep_going.store(false, std::memory_order_relaxed);
    producer.join();

    auto thread = logger->take_thread();
    thread.join(); // graceful stop: waits for the drain to empty the lanes

    ASSERT_TRUE(logger->mailbox()->idle());
  }
}

// --- quoted_t value helper -------------------------------------------------

static void test_quoted_t() {
  // Safe values render verbatim.
  ASSERT_EQUAL(FMT::format("{}", quoted_t{"hello"}), "hello");
  // Spaces, '=', and '"' trigger double-quoting.
  ASSERT_EQUAL(FMT::format("{}", quoted_t{"hello world"}), "\"hello world\"");
  ASSERT_EQUAL(FMT::format("{}", quoted_t{"a=b"}), "\"a=b\"");
  ASSERT_EQUAL(FMT::format("{}", quoted_t{"he said \"hi\""}),
               "\"he said \\\"hi\\\"\"");
  // Empty value renders bare.
  ASSERT_EQUAL(FMT::format("{}", quoted_t{""}), "");
  // Control characters are escaped inside quotes (one-line invariant).
  ASSERT_EQUAL(FMT::format("{}", quoted_t{"a\nb"}), "\"a\\nb\"");
  ASSERT_EQUAL(FMT::format("{}", quoted_t{"a\tb"}), "\"a\\tb\"");
}

// --- color highlighting ----------------------------------------------------

static constexpr const char *BLUE = "\033[1;34m";
static constexpr const char *YELLOW = "\033[1;33m";
static constexpr const char *RED = "\033[1;31m";
static constexpr const char *RESET = "\033[0m";

static void test_colorizer() {
  rtpmidid::set_log_color_enabled(true);
  const rtpmidid::log_message_t msg{rtpmidid::logger_level_t::INFO, "f.cpp", 1,
                                    "name=hello error=\"a=b c\""};
  const std::string line = rtpmidid::logger_format_line(msg);

  // INFO level carries no color.
  ASSERT_TRUE(line.find("level=info") != std::string::npos);
  ASSERT_TRUE(line.find(std::string(BLUE) + "level" + RESET) ==
              std::string::npos);
  // Built-in keys are blue.
  ASSERT_TRUE(line.find(std::string(BLUE) + "thread" + RESET) !=
              std::string::npos);
  ASSERT_TRUE(line.find(std::string(BLUE) + "filename" + RESET) !=
              std::string::npos);
  // Custom keys are yellow.
  ASSERT_TRUE(line.find(std::string(YELLOW) + "name" + RESET) !=
              std::string::npos);
  ASSERT_TRUE(line.find(std::string(YELLOW) + "error" + RESET) !=
              std::string::npos);
  // The '=' inside the quoted value is not treated as a key: the value
  // stays intact.
  ASSERT_TRUE(line.find("\"a=b c\"") != std::string::npos);
  rtpmidid::set_log_color_enabled(false);
}

static void test_level_severity_color() {
  rtpmidid::set_log_color_enabled(true);
  // The `level=` token keeps the old per-level colors.
  const std::string e = rtpmidid::logger_format_line(
      rtpmidid::log_message_t{rtpmidid::logger_level_t::ERROR, "f.cpp", 1, "boom"});
  ASSERT_TRUE(e.find(std::string(RED) + "level=error" + RESET) !=
              std::string::npos);
  const std::string w = rtpmidid::logger_format_line(
      rtpmidid::log_message_t{rtpmidid::logger_level_t::WARNING, "f.cpp", 1, "careful"});
  ASSERT_TRUE(w.find(std::string(YELLOW) + "level=warning" + RESET) !=
              std::string::npos);
  const std::string d = rtpmidid::logger_format_line(
      rtpmidid::log_message_t{rtpmidid::logger_level_t::DEBUG, "f.cpp", 1, "noise"});
  ASSERT_TRUE(d.find(std::string(BLUE) + "level=debug" + RESET) !=
              std::string::npos);
  const std::string i = rtpmidid::logger_format_line(
      rtpmidid::log_message_t{rtpmidid::logger_level_t::INFO, "f.cpp", 1, "info"});
  ASSERT_TRUE(i.find("level=info") != std::string::npos);
  ASSERT_TRUE(i.find(std::string(BLUE) + "level=info") == std::string::npos);
  rtpmidid::set_log_color_enabled(false);
}

static void test_body_alignment() {
  rtpmidid::set_log_color_enabled(false);
  const std::string a = rtpmidid::logger_format_line(
      rtpmidid::log_message_t{rtpmidid::logger_level_t::INFO, "f.cpp", 1, "alpha=1"});
  const std::string b = rtpmidid::logger_format_line(rtpmidid::log_message_t{
      rtpmidid::logger_level_t::INFO, "alsa_actor.cpp", 123, "beta=2", "router"});
  // The producer's message starts at the same column in both lines.
  ASSERT_EQUAL(a.find("alpha=1"), b.find("beta=2"));
}

static void test_color_off_skips_coloring() {
  rtpmidid::set_log_color_enabled(false);
  const rtpmidid::log_message_t msg{rtpmidid::logger_level_t::INFO, "f.cpp", 1,
                                    "name=hello"};
  const std::string line = rtpmidid::logger_format_line(msg);
  ASSERT_TRUE(line.find("\033[") == std::string::npos);
}

// --- color configuration precedence ----------------------------------------

static void test_color_config_precedence() {
  using rtpmidid::compute_log_color_enabled;
  // --log-no-color wins over everything.
  ASSERT_FALSE(compute_log_color_enabled(true, false, true, false, false, true));
  // NO_COLOR beats FORCE_COLOR.
  ASSERT_FALSE(
      compute_log_color_enabled(false, true, true, false, false, true));
  // FORCE_COLOR enables even when not a TTY, and beats INI never.
  ASSERT_TRUE(
      compute_log_color_enabled(false, false, true, false, false, false));
  ASSERT_TRUE(compute_log_color_enabled(false, false, true, true, false, false));
  // INI never / always.
  ASSERT_FALSE(
      compute_log_color_enabled(false, false, false, true, false, true));
  ASSERT_TRUE(
      compute_log_color_enabled(false, false, false, false, true, false));
  // Default: TTY detection.
  ASSERT_TRUE(
      compute_log_color_enabled(false, false, false, false, false, true));
  ASSERT_FALSE(
      compute_log_color_enabled(false, false, false, false, false, false));
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_actor_formats_and_prints),
      TEST(test_thread_tag_rendering),
      TEST(test_thread_tag_capture_and_legacy),
      TEST(test_tag_survives_producer_exit),
      TEST(test_macros_route_through_sink),
      TEST(test_producer_level_filter),
      TEST(test_stop_drains_queue),
      TEST(test_no_sink_prints_directly),
      TEST(test_threaded_stop_drains_racing_producer),
      TEST(test_quoted_t),
      TEST(test_colorizer),
      TEST(test_level_severity_color),
      TEST(test_body_alignment),
      TEST(test_color_off_skips_coloring),
      TEST(test_color_config_precedence),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

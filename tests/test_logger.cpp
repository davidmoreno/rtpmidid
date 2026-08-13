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

/// Logger actor tests (threadless pump mode): exact line rendering, macro
/// routing through the installed sink, producer-side level filtering, the
/// stop-drains-the-queue guarantee, the direct-print fallback when no
/// actor is installed, and the per-thread log tag (rendering, capture,
/// survival across producer exit).

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

  logger.mailbox()->post_data(rtpmidid::log_message_t{
      rtpmidid::logger_level_t::INFO, "myfile.cpp:42", "hello world"});
  logger.pump();

  // "[INFO ] myfile.cpp:42" (21 chars) padded to 40, " | hello world",
  // color reset, newline. INFO carries no color.
  const std::string expected = std::string("[INFO ] myfile.cpp:42") +
                               std::string(19, ' ') +
                               " | hello world\033[0m\n";
  ASSERT_EQUAL(capture.str(), expected);

  // A DEBUG message gets the ANSI color prefix.
  cout_capture_t capture2;
  logger.mailbox()->post_data(rtpmidid::log_message_t{
      rtpmidid::logger_level_t::DEBUG, "dbg.cpp:7", "noise"});
  logger.pump();
  ASSERT_TRUE(capture2.str().find("\033[1;34m[DEBUG] dbg.cpp:7") == 0);
  ASSERT_TRUE(capture2.str().find("noise") != std::string::npos);
}

static void test_macros_route_through_sink() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  sink_guard_t guard(logger);
  {
    cout_capture_t capture;
    INFO("routed {}", 42);
    WARNING("careful {}", "now");
    logger.pump();
    ASSERT_TRUE(capture.str().find("routed 42") != std::string::npos);
    ASSERT_TRUE(capture.str().find("[WARN ]") != std::string::npos);
    ASSERT_TRUE(capture.str().find("careful now") != std::string::npos);
  }
}

static void test_producer_level_filter() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  sink_guard_t guard(logger);

  rtpmidid::logger2.set_log_level(rtpmidid::logger_level_t::ERROR);
  cout_capture_t capture;
  INFO("filtered below ERROR {}", 1);
  logger.pump();
  // The producer filtered the message: nothing was posted, nothing printed.
  ASSERT_TRUE(capture.str().empty());

  rtpmidid::logger2.set_log_level(rtpmidid::logger_level_t::INFO);
  cout_capture_t capture2;
  ERROR("still routed {}", 2);
  logger.pump();
  ASSERT_TRUE(capture2.str().find("still routed 2") != std::string::npos);
  rtpmidid::logger2.set_log_level(rtpmidid::logger_level_t::INFO);
}

static void test_stop_drains_queue() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  cout_capture_t capture;

  for (int i = 0; i < 5; i++) {
    logger.mailbox()->post_data(rtpmidid::log_message_t{
        rtpmidid::logger_level_t::INFO, "flood.cpp:1",
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
      rtpmidid::logger_level_t::INFO, "late.cpp:1", "after stop"});
}

static void test_no_sink_prints_directly() {
  rtpmidid::logger_log_sink = nullptr; // no actor installed
  cout_capture_t capture;
  INFO("direct {}", 7);
  ASSERT_TRUE(capture.str().find("direct 7") != std::string::npos);
}

/// Tagged messages render the tag inside the padded prefix, with the `|`
/// aligned at the same column as untagged lines.
static void test_thread_tag_rendering() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  cout_capture_t capture;

  logger.mailbox()->post_data(rtpmidid::log_message_t{
      rtpmidid::logger_level_t::INFO, "router_actor.cpp:12", "peer up",
      "router"});
  logger.pump();

  // "[INFO ] [router] router_actor.cpp:12" (36 chars) padded to 40, then
  // " | peer up", color reset, newline. INFO carries no color.
  const std::string expected =
      std::string("[INFO ] [router] router_actor.cpp:12") +
      std::string(4, ' ') + " | peer up\033[0m\n";
  ASSERT_EQUAL(capture.str(), expected);
}

/// The thread tag is captured at production time: a thread that set a tag
/// produces tagged lines through the sink, and an untagged thread produces
/// the legacy byte-identical format.
static void test_thread_tag_capture_and_legacy() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  sink_guard_t guard(logger);
  {
    rtpmidid::set_log_thread_tag("worker");
    cout_capture_t capture;
    INFO("tagged from worker");
    logger.pump();
    ASSERT_TRUE(capture.str().find("[INFO ] [worker] test_logger.cpp:") !=
                std::string::npos);
    ASSERT_TRUE(capture.str().find("tagged from worker") !=
                std::string::npos);
  }
  {
    rtpmidid::set_log_thread_tag(""); // untagged: legacy format, no tag
    cout_capture_t capture;
    INFO("untagged message");
    logger.pump();
    ASSERT_TRUE(capture.str().find("[INFO ] test_logger.cpp:") !=
                std::string::npos);
    ASSERT_TRUE(capture.str().find("untagged message") !=
                std::string::npos);
  }
}

/// The tag is an owned copy, so it survives the producing thread exiting
/// before the logger renders the message — no dangling reference.
static void test_tag_survives_producer_exit() {
  logger_actor_t logger(actor_config_t{.name = "logger"});
  sink_guard_t guard(logger);
  cout_capture_t capture;

  std::thread producer([] {
    rtpmidid::set_log_thread_tag("ephemeral");
    INFO("from a dying thread");
  });
  producer.join();
  logger.pump();

  ASSERT_TRUE(capture.str().find("[ephemeral]") != std::string::npos);
  ASSERT_TRUE(capture.str().find("from a dying thread") !=
              std::string::npos);
}

/// Real-thread hammering of the drain-on-stop guarantee: a producer
/// thread posts continuously while the main thread stops the actor, so
/// the stop races the in-progress drain. After the thread exits the
/// mailbox must be empty — the graceful stop never drops messages
/// enqueued before (or racing) it.
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
            rtpmidid::logger_level_t::INFO, "race.cpp:1",
            "msg " + std::to_string(i++)});
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    logger->request_stop(); // races the in-progress drain
    keep_going.store(false, std::memory_order_relaxed);
    producer.join();

    auto thread = logger->take_thread();
    thread.join(); // graceful stop: waits for the drain to empty the lanes

    // Everything enqueued before/around the stop was drained.
    ASSERT_TRUE(logger->mailbox()->idle());
  }
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
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

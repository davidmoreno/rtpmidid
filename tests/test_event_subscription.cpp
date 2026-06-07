/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * Tests for the event_subscription_manager_t used by WebSocket connections.
 */
#include "./test_case.hpp"
#include "../src/control_rpc.hpp"
#include "../src/event_subscription.hpp"
#include <rtpmidid/logger.hpp>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Global mDNS instance required by static library (peer_export_rtpmidi_server, etc.)
namespace rtpmididns {
std::shared_ptr<rtpmidid::mdns_rtpmidi_t> mdns;
}

using namespace rtpmididns;

static void test_subscribe_unsubscribe() {
  auto mgr = std::make_shared<event_subscription_manager_t>();

  // Set up send callback
  std::vector<std::string> sent;
  std::atomic<int> send_count{0};
  mgr->set_send_fn([&](const std::string &json) {
    sent.push_back(json);
    send_count.fetch_add(1);
  });

  // Subscribe
  mgr->subscribe({"router.peer_added", "router.edge_added"});
  ASSERT_TRUE(mgr->is_subscribed("router.peer_added"));
  ASSERT_TRUE(mgr->is_subscribed("router.edge_added"));
  ASSERT_TRUE(!mgr->is_subscribed("router.peer_removed"));

  // Emit → should arrive
  mgr->emit("router.peer_added", R"({"id":42,"name":"test"})");
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT_EQUAL(send_count.load(), 1);
  ASSERT_TRUE(sent.back().find("\"event\":\"router.peer_added\"") !=
              std::string::npos);
  ASSERT_TRUE(sent.back().find("\"id\":42") != std::string::npos);

  // Emit unsubscribed → should NOT arrive
  mgr->emit("router.peer_removed", R"({"peer_id":42})");
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT_EQUAL(send_count.load(), 1); // still 1

  // Unsubscribe
  mgr->unsubscribe({"router.peer_added"});
  ASSERT_TRUE(!mgr->is_subscribed("router.peer_added"));
  ASSERT_TRUE(mgr->is_subscribed("router.edge_added"));

  // Emit unsubscribed
  mgr->emit("router.peer_added", R"({"id":99})");
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT_EQUAL(send_count.load(), 1);

  // Emit still-subscribed
  mgr->emit("router.edge_added", R"({"from":1,"to":2})");
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT_EQUAL(send_count.load(), 2);

  // Unsubscribe all
  mgr->unsubscribe_all();
  ASSERT_TRUE(!mgr->is_subscribed("router.edge_added"));
}

static void test_thread_safety() {
  auto mgr = std::make_shared<event_subscription_manager_t>();
  std::atomic<int> send_count{0};
  mgr->set_send_fn([&](const std::string &) { send_count.fetch_add(1); });

  mgr->subscribe({"ch.a", "ch.b"});

  // Fire events from multiple threads concurrently
  const int num_threads = 4;
  const int events_per_thread = 100;
  std::vector<std::thread> threads;
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([mgr, events_per_thread, t]() {
      for (int i = 0; i < events_per_thread; ++i) {
        if (i % 2 == 0)
          mgr->emit("ch.a", R"({"i":)" + std::to_string(i) + "}");
        else
          mgr->emit("ch.b", R"({"i":)" + std::to_string(i) + "}");
      }
    });
  }

  // While events are firing, also subscribe/unsubscribe from main thread
  for (int i = 0; i < 20; ++i) {
    mgr->subscribe({"ch.c"});
    mgr->unsubscribe({"ch.c"});
  }

  for (auto &th : threads)
    th.join();

  ASSERT_EQUAL(send_count.load(), num_threads * events_per_thread);
}

static void test_control_rpc_subscribe() {
  auto mgr = std::make_shared<event_subscription_manager_t>();
  control_rpc_context_t ctx{};
  ctx.subscriptions = mgr;

  // Call subscribe via RPC dispatcher
  std::string result =
      control_rpc_dispatch_line(ctx, R"({"method":"subscribe","params":{"channels":["ch.a","ch.b"]},"id":1})");
  ASSERT_TRUE(result.find("\"result\":[\"ok\"]") != std::string::npos);
  ASSERT_TRUE(mgr->is_subscribed("ch.a"));
  ASSERT_TRUE(mgr->is_subscribed("ch.b"));

  // Unsubscribe
  result = control_rpc_dispatch_line(
      ctx, R"({"method":"unsubscribe","params":{"channels":["ch.a"]},"id":2})");
  ASSERT_TRUE(result.find("\"result\":[\"ok\"]") != std::string::npos);
  ASSERT_TRUE(!mgr->is_subscribed("ch.a"));
  ASSERT_TRUE(mgr->is_subscribed("ch.b"));

  // Subscribe without subscriptions → error
  control_rpc_context_t ctx_no_subs{};
  result = control_rpc_dispatch_line(
      ctx_no_subs,
      R"({"method":"subscribe","params":{"channels":["x"]},"id":3})");
  ASSERT_TRUE(result.find("\"error\"") != std::string::npos);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_subscribe_unsubscribe),
      TEST(test_thread_safety),
      TEST(test_control_rpc_subscribe),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

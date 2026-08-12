/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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

/// Control socket actor tests (tasks 7.1-7.4): wire-shape responses over a
/// real unix socket, the requester-driven status gather, deadline error
/// responses, and per-connection isolation (a stalled client does not
/// block others).

#include "control_socket_actor.hpp"
#include "messages.hpp"
#include "router_actor.hpp"
#include "test_case.hpp"
#include "worker_actor.hpp"
#include <chrono>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using namespace rtpmididns;

// --- a peer that answers status and command requests ------------------------

class answering_peer_t : public actor_t<std::monostate, control_message_t> {
public:
  using actor_t::actor_t;
  void on_control(control_message_t &&msg) override {
    std::visit(
        [this](auto &&m) {
          using T = std::decay_t<decltype(m)>;
          if constexpr (std::is_same_v<T, peer_status_req_t>) {
            rtp_peer_status_t st;
            st.name = "answerer";
            m.reply_to.post_control(
                peer_status_resp_t{m.hdr, m.target, st});
          } else if constexpr (std::is_same_v<T, peer_command_t>) {
            std::string result = m.cmd == "status"
                                     ? R"({"name":"answerer"})"
                                     : R"({"error":"Command not implemented"})";
            m.reply_to.post_control(peer_command_resp_t{
                m.hdr, m.peer_id, result, m.cmd != "status"});
          }
        },
        msg);
  }
};

// --- unix client helpers -----------------------------------------------------

static int connect_unix(const std::string &path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  if (::connect(fd, (const struct sockaddr *)&addr,
                sizeof(struct sockaddr_un)) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

static std::string read_response(int fd, int timeout_ms = 5000) {
  std::string out;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    char buf[4096];
    const ssize_t n = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n > 0) {
      out.append(buf, n);
      if (out.find('\n') != std::string::npos) {
        return out;
      }
    } else if (n == 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return out;
}

static bool wait_until(const std::function<bool()> &f, int timeout_ms = 5000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (f()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return f();
}

// --- tests ------------------------------------------------------------------

void test_control_socket_wire_roundtrip() {
  const std::string socket_path =
      "/tmp/rtpmidid-test-control-" + std::to_string(::getpid()) + ".sock";
  auto supervisor = std::make_shared<actor_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = supervisor});
  auto worker = std::make_shared<worker_actor_t>(actor_config_t{.name = "w"});
  router->start();
  worker->start();

  // A registered peer that answers status/commands.
  auto peer = std::make_shared<answering_peer_t>(
      actor_config_t{.name = "p", .supervisor_mailbox = supervisor});
  peer->start();
  auto req = std::make_shared<actor_mailbox_t>();
  router->mailbox()->post_control(
      register_peer_t{hdr_t{1}, req, peer->mailbox(), "test", "p"});
  ASSERT_TRUE(wait_until([&] { return !req->idle(); }));
  peer_id_t peer_id = 0;
  while (auto c = req->pop_control()) {
    if (auto *r = std::get_if<peer_ids_result_t>(&*c)) {
      peer_id = r->ids[0];
    }
  }
  ASSERT_NOT_EQUAL(peer_id, 0u);

  auto listener = std::make_shared<control_listener_actor_t>(
      actor_config_t{.name = "ctl", .supervisor_mailbox = supervisor},
      socket_path, router->mailbox(), nullptr, worker, "test-version",
      std::chrono::milliseconds(1000));
  listener->start();
  ASSERT_TRUE(wait_until([&] { return listener->is_running(); }));

  int fd = -1;
  for (int i = 0; i < 400 && fd < 0; i++) {
    fd = connect_unix(socket_path);
    if (fd < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  ASSERT_GTE(fd, 0);

  // help: synchronous result with the id echoed.
  std::string help = "{\"method\":\"help\",\"id\":\"1\"}\n";
  ::write(fd, help.data(), help.size());
  auto r1 = read_response(fd);
  ASSERT_TRUE(r1.find("\"id\":\"1\"") != std::string::npos);
  ASSERT_TRUE(r1.find("\"result\"") != std::string::npos);
  ASSERT_TRUE(r1.find("\"name\":\"status\"") != std::string::npos);

  // unknown method -> error.
  std::string bogus = "{\"method\":\"bogus\",\"id\":\"2\"}\n";
  ::write(fd, bogus.data(), bogus.size());
  auto r2 = read_response(fd);
  ASSERT_TRUE(r2.find("\"id\":\"2\"") != std::string::npos);
  ASSERT_TRUE(r2.find("\"error\"") != std::string::npos);
  ASSERT_TRUE(r2.find("Unknown method") != std::string::npos);

  // peer command relay: "1.status".
  std::string peer_cmd =
      "{\"method\":\"" + std::to_string(peer_id) + ".status\",\"id\":\"3\"}\n";
  ::write(fd, peer_cmd.data(), peer_cmd.size());
  auto r3 = read_response(fd);
  ASSERT_TRUE(r3.find("\"id\":\"3\"") != std::string::npos);
  ASSERT_TRUE(r3.find("\"name\":\"answerer\"") != std::string::npos);

  // status: requester-driven gather with the registered peer.
  std::string status_cmd = "{\"method\":\"status\",\"id\":\"4\"}\n";
  ::write(fd, status_cmd.data(), status_cmd.size());
  auto r4 = read_response(fd, 8000);
  ASSERT_TRUE(r4.find("\"id\":\"4\"") != std::string::npos);
  ASSERT_TRUE(r4.find("\"router\"") != std::string::npos);
  ASSERT_TRUE(r4.find("\"version\":\"test-version\"") != std::string::npos);
  ASSERT_TRUE(r4.find("\"name\":\"answerer\"") != std::string::npos);
  // mdns was not configured: status "Not available".
  ASSERT_TRUE(r4.find("Not available") != std::string::npos);

  // unknown peer command -> error (async, router relays, peer id unknown).
  std::string bad_peer = "{\"method\":\"999.status\",\"id\":\"5\"}\n";
  ::write(fd, bad_peer.data(), bad_peer.size());
  auto r5 = read_response(fd);
  ASSERT_TRUE(r5.find("\"id\":\"5\"") != std::string::npos);
  ASSERT_TRUE(r5.find("Unknown peer") != std::string::npos);

  ::close(fd);
  listener->request_stop();
  router->request_stop();
  worker->request_stop();
  peer->request_stop();
  ::unlink(socket_path.c_str());
}

void test_stalled_client_does_not_block_others() {
  const std::string socket_path =
      "/tmp/rtpmidid-test-control-" + std::to_string(::getpid()) + "-b.sock";
  auto supervisor = std::make_shared<actor_mailbox_t>();
  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router", .supervisor_mailbox = supervisor});
  auto worker = std::make_shared<worker_actor_t>(actor_config_t{.name = "w"});
  router->start();
  worker->start();

  // A silent mailbox registered as a peer: status gathers wait on it.
  auto silent = std::make_shared<actor_mailbox_t>();
  auto req = std::make_shared<actor_mailbox_t>();
  router->mailbox()->post_control(
      register_peer_t{hdr_t{1}, req, silent, "test", "silent"});
  ASSERT_TRUE(wait_until([&] { return !req->idle(); }));

  auto listener = std::make_shared<control_listener_actor_t>(
      actor_config_t{.name = "ctl", .supervisor_mailbox = supervisor},
      socket_path, router->mailbox(), nullptr, worker, "v",
      std::chrono::milliseconds(400));
  listener->start();

  int slow = -1, fast = -1;
  for (int i = 0; i < 400 && (slow < 0 || fast < 0); i++) {
    if (slow < 0) {
      slow = connect_unix(socket_path);
    }
    if (fast < 0) {
      fast = connect_unix(socket_path);
    }
    if (slow < 0 || fast < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  ASSERT_GTE(slow, 0);
  ASSERT_GTE(fast, 0);

  // The slow client issues a status gather that can only complete via the
  // deadline (the silent peer never answers).
  std::string status_cmd = "{\"method\":\"status\",\"id\":\"9\"}\n";
  ::write(slow, status_cmd.data(), status_cmd.size());

  // The fast client's help completes promptly, before the slow one's
  // deadline: a stalled client does not block others.
  const auto t0 = std::chrono::steady_clock::now();
  std::string help_cmd = "{\"method\":\"help\",\"id\":\"2\"}\n";
  ::write(fast, help_cmd.data(), help_cmd.size());
  auto fast_resp = read_response(fast, 2000);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - t0)
          .count();
  ASSERT_TRUE(fast_resp.find("\"id\":\"2\"") != std::string::npos);
  ASSERT_LT(elapsed, 1500);

  // The slow client eventually gets a deadline-based error response.
  auto slow_resp = read_response(slow, 4000);
  ASSERT_TRUE(slow_resp.find("\"id\":\"9\"") != std::string::npos);
  ASSERT_TRUE(slow_resp.find("\"error\"") != std::string::npos);

  ::close(slow);
  ::close(fast);
  listener->request_stop();
  router->request_stop();
  worker->request_stop();
  ::unlink(socket_path.c_str());
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_control_socket_wire_roundtrip),
      TEST(test_stalled_client_does_not_block_others),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

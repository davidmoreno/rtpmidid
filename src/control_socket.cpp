/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
 */
#include "control_socket.hpp"
#include "control_rpc.hpp"
#include "settings.hpp"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include "json.hpp"
#include "stringpp.hpp"

namespace rtpmididns {

const char *const MSG_CLOSE_CONN =
    "{\"event\": \"close\", \"detail\": \"Shutdown\", \"code\": 0}\n";
const char *const MSG_TOO_LONG =
    "{\"event\": \"close\", \"detail\": \"Message too long\", \"code\": 1}\n";

namespace {

ssize_t control_conn_send(int fd, const void *buf, size_t len) {
#if defined(__linux__) && defined(MSG_NOSIGNAL)
  return ::send(fd, buf, len, MSG_NOSIGNAL);
#else
  return ::write(fd, buf, len);
#endif
}

void send_control_goodbye(int fd) {
  (void)control_conn_send(fd, MSG_CLOSE_CONN, strlen(MSG_CLOSE_CONN));
}

} // namespace

control_socket_t::control_socket_t() {
  std::string &socketfile = settings.control_filename;

  int ret = unlink(socketfile.c_str());
  if (ret >= 0) {
    INFO("Removed old control socket. Creating new one.");
  }

  socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket == -1) {
    ERROR("Error creating socket: {}", strerror(errno));
    return;
  }
  struct sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socketfile.c_str(), sizeof(addr.sun_path) - 1);

  ret = bind(socket, (const struct sockaddr *)(&addr),
             sizeof(struct sockaddr_un));
  if (ret == -1) {
    ERROR("Error Binding socket at {}: {}", socketfile, strerror(errno));
    close(socket);
    socket = -1;
    return;
  }
  if (listen(socket, 20) == -1) {
    ERROR("Error Listening to socket at {}: {}", socketfile, strerror(errno));
    close(socket);
    socket = -1;
    return;
  }
  ::chmod(socketfile.c_str(), 0777);
  INFO("Control socket ready at {}", socketfile);
  start_time = time(NULL);

  server_running_.store(true, std::memory_order_release);
  try {
    server_thread_ = std::thread(&control_socket_t::server_thread_main, this);
  } catch (const std::exception &e) {
    server_running_.store(false, std::memory_order_release);
    ERROR("Could not start control socket thread: {}", e.what());
    ::close(socket);
    socket = -1;
  }
}

rtpmididns::control_socket_t::~control_socket_t() noexcept {
  server_running_.store(false, std::memory_order_release);
  if (server_thread_.joinable()) {
    if (socket >= 0) {
      (void)::shutdown(socket, SHUT_RDWR);
    }
    server_thread_.join();
  }
  if (socket >= 0) {
    ::close(socket);
    socket = -1;
  }
  DEBUG("Closed control socket");
}

void control_socket_t::server_thread_main() {
  const int listen_fd = socket;
  if (listen_fd < 0) {
    return;
  }

#if !defined(_WIN32)
  (void)::signal(SIGPIPE, SIG_IGN);
#endif

  std::vector<struct pollfd> pfds;
  pfds.reserve(16);
  pfds.push_back({listen_fd, POLLIN, 0});

  while (server_running_.load(std::memory_order_acquire)) {
    const int pr = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 1000);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    for (size_t i = 0; i < pfds.size();) {
      struct pollfd &p = pfds[i];
      if (p.revents == 0) {
        ++i;
        continue;
      }

      if (p.fd == listen_fd) {
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
          ++i;
          continue;
        }
        if ((p.revents & POLLIN) &&
            server_running_.load(std::memory_order_acquire)) {
          const int cfd = ::accept(listen_fd, nullptr, nullptr);
          if (cfd >= 0) {
            pfds.push_back({cfd, POLLIN, 0});
          } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
          }
        }
        p.revents = 0;
        ++i;
        continue;
      }

      if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        send_control_goodbye(p.fd);
        ::close(p.fd);
        pfds.erase(pfds.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }
      if (p.revents & POLLIN) {
        const bool close_me = handle_client_data(p.fd);
        if (close_me) {
          ::close(p.fd);
          pfds.erase(pfds.begin() + static_cast<std::ptrdiff_t>(i));
          continue;
        }
      }
      p.revents = 0;
      ++i;
    }
  }

  for (size_t i = 1; i < pfds.size(); ++i) {
    send_control_goodbye(pfds[i].fd);
    ::close(pfds[i].fd);
  }
}

bool control_socket_t::handle_client_data(int fd) {
  char buf[1024]; // NOLINT
  const ssize_t l = recv(fd, buf, sizeof(buf), 0);
  if (l <= 0) {
    send_control_goodbye(fd);
    return true;
  }
  if (static_cast<size_t>(l) >= sizeof(buf) - 1) {
    const ssize_t w =
        control_conn_send(fd, MSG_TOO_LONG, strlen(MSG_TOO_LONG));
    if (w < 0) {
      ERROR(
          "Could not send msg too long to control socket! Closing connection.");
    }
    return true;
  }
  buf[static_cast<size_t>(l)] = 0;
  control_rpc_context_t ctx{router, aseq, mdns};
  json_t js;
  try {
    js = json_t::parse(trim_copy(buf));
  } catch (const std::exception &e) {
    auto err = json_t{{"error", e.what()}};
    const std::string out = err.dump() + "\n";
    (void)control_conn_send(fd, out.c_str(), out.length());
    return false;
  }
  const json_t ret = control_rpc_dispatch(ctx, js);
  const std::string retstr = ret.dump() + "\n";
  const ssize_t w = control_conn_send(fd, retstr.c_str(), retstr.length());
  if (w < 0) {
    ERROR("Could not send msg to control socket! Closing Connection.");
    return true;
  }
  return false;
}

std::string control_socket_t::parse_command(const std::string &command) {
  json_t js;
  try {
    js = json_t::parse(command);
  } catch (const std::exception &e) {
    return json_t{{"error", e.what()}}.dump();
  }
  control_rpc_context_t ctx{router, aseq, mdns};
  return control_rpc_dispatch(ctx, js).dump();
}

} // namespace rtpmididns

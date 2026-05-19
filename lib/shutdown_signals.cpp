/**
 * Async-signal-safe shutdown: eventfd + sigaction for SIGINT/SIGTERM.
 */
#include <rtpmidid/shutdown_signals.hpp>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace rtpmidid {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_shutdown_eventfd = -1;

static void fill_shutdown_sigset(sigset_t *set) {
  sigemptyset(set);
  sigaddset(set, SIGINT);
  sigaddset(set, SIGTERM);
}

static void async_signal_handler(int sig) {
  if (g_shutdown_eventfd >= 0) {
    const uint64_t one = 1;
    (void)::write(g_shutdown_eventfd, &one, sizeof one);
  }
  static volatile sig_atomic_t fired = 0;
  if (fired != 0) {
    struct sigaction sa {};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(sig, &sa, nullptr);
    raise(sig);
  }
  fired = 1;
}

} // namespace

void block_shutdown_signals() {
  sigset_t set {};
  fill_shutdown_sigset(&set);
  (void)pthread_sigmask(SIG_BLOCK, &set, nullptr);
}

void unblock_shutdown_signals() {
  sigset_t set {};
  fill_shutdown_sigset(&set);
  (void)pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
}

void install_shutdown_signal_handlers(int shutdown_eventfd) {
  g_shutdown_eventfd = shutdown_eventfd;

  struct sigaction sa {};
  sa.sa_handler = async_signal_handler;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGINT);
  sigaddset(&sa.sa_mask, SIGTERM);
  sa.sa_flags = 0;

  (void)sigaction(SIGINT, &sa, nullptr);
  (void)sigaction(SIGTERM, &sa, nullptr);
}

} // namespace rtpmidid

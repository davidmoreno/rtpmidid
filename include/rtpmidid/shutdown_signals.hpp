/**
 * Async-signal-safe shutdown: eventfd + sigaction for SIGINT/SIGTERM.
 */
#pragma once

namespace rtpmidid {

/** Block SIGINT and SIGTERM on the calling thread (worker threads). */
void block_shutdown_signals();

/** Unblock SIGINT and SIGTERM on the calling thread (main thread only). */
void unblock_shutdown_signals();

/**
 * Install SIGINT/SIGTERM handlers that write to @p shutdown_eventfd.
 * @p shutdown_eventfd must stay valid until handlers are replaced.
 */
void install_shutdown_signal_handlers(int shutdown_eventfd);

} // namespace rtpmidid

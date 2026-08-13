// Ticker messages — the worked example from docs/actors/creating-actors.md.
#pragma once
#include "mailbox.hpp"
#include "message_core.hpp"

namespace rtpmididns {

/// Configure the ticker: emit a tick to `target` every `period_ms`.
struct ticker_set_t {
  hdr_t hdr;
  std::chrono::milliseconds period{1000};
  mailbox_handle_t target;
};

/// A tick emitted to the configured target (echoes the set request's corr).
struct ticker_tick_t {
  hdr_t hdr;
  uint64_t sequence = 0;
};

/// The ticker's accepted control messages.
using ticker_control_t = std::variant<stop_t, ticker_set_t>;
/// The ticker's mailbox (no data lane: DataT = std::monostate).
using ticker_mailbox_t = mailbox_t<std::monostate, ticker_control_t>;

} // namespace rtpmididns

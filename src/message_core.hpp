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

/// The actor message core: the types every actor shares — ids, the
/// request/response envelope, the compile-time acceptance trait, the
/// type-erased mailbox handle, the universal lifecycle messages, and the
/// generic reply envelopes (`ack`, assigned-ids result). Subsystem
/// messages live next to their actors (router_messages.hpp,
/// peer_messages.hpp, ...); messages.hpp assembles the transport envelope.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace rtpmididns {

// The mailbox joins two lanes of the types below; the template is declared
// here and defined in mailbox.hpp. `mailbox_base_t` erases the lane types
// for cross-actor handles (messages carry `shared_ptr` handles so posting
// to a terminated actor's mailbox stays safe).
template <typename DataT, typename ControlT> class mailbox_t;
class mailbox_base_t;
class actor_base_t;
template <typename DataT, typename ControlT> class actor_t;
struct data_message_t;

using peer_id_t = uint32_t;
using actor_id_t = uint32_t;

/**
 * Compile-time trait: is `M` one of the alternatives of the `Variant`?
 * Used to type-check which messages a queue/actor accepts (each actor
 * declares its accepted control messages as a `std::variant`; the queue
 * element type IS that variant, so posting a message the actor does not
 * accept is a compile error on the typed path).
 */
template <typename M, typename Variant> struct is_alternative : std::false_type {};
template <typename M, typename... Ts>
struct is_alternative<M, std::variant<Ts...>>
    : std::bool_constant<(std::is_same_v<M, Ts> || ...)> {};
template <typename M, typename Variant>
inline constexpr bool is_alternative_v = is_alternative<M, Variant>::value;

/// Request/response envelope (design D7): correlation id + optional reply
/// mailbox handle for requester-driven flows.
struct hdr_t {
  uint64_t corr = 0;
};

/**
 * Requester-side deadline helper (design D7/D15): tracks pending requests
 * keyed by correlation id with requester-side deadlines. Backs the router's
 * pending-table dispatch (busy actors that cannot park a selective wait).
 * Deadlines are enforced exclusively by the requester; responders never
 * know about them. Responses with unknown or expired correlation ids are
 * silently discarded (`is_pending` returns false).
 */
class pending_request_table_t {
public:
  void add(uint64_t corr, std::chrono::milliseconds timeout) {
    entries_[corr] = std::chrono::steady_clock::now() + timeout;
  }

  /// True while the request is live (present and not past its deadline).
  bool is_pending(uint64_t corr) const {
    auto it = entries_.find(corr);
    if (it == entries_.end()) {
      return false;
    }
    if (std::chrono::steady_clock::now() >= it->second) {
      return false; // expired: treated as unknown, silently discarded
    }
    return true;
  }

  void erase(uint64_t corr) { entries_.erase(corr); }
  /// Drop expired entries (housekeeping; lookups already handle expiry).
  void expire() {
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(entries_, [&](const auto &kv) { return now >= kv.second; });
  }
  size_t size() const { return entries_.size(); }

private:
  std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> entries_;
};

/**
 * Type-erased mailbox handle (design D6): points at any actor mailbox and
 * can post any control message (checked at compile time against the
 * transport envelope; the target mailbox validates the message against
 * its own accepted set and drops it — with a warning — if it does not
 * accept it). Used for `reply_to` and other cross-actor handles where the
 * concrete mailbox lane types are not known at the call site.
 */
class mailbox_handle_t {
public:
  mailbox_handle_t() = default;
  mailbox_handle_t(std::shared_ptr<mailbox_base_t> mb)
      : mb_(std::move(mb)) {}
  template <typename Mailbox> mailbox_handle_t(std::shared_ptr<Mailbox> mb)
      : mb_(std::move(mb)) {}

  explicit operator bool() const { return mb_ != nullptr; }
  bool operator==(std::nullptr_t) const { return mb_ == nullptr; }
  bool operator!=(std::nullptr_t) const { return mb_ != nullptr; }
  bool operator==(const mailbox_handle_t &o) const { return mb_ == o.mb_; }

  /// Typed control post through the erased handle (defined in mailbox.hpp,
  /// where the transport envelope is complete).
  template <typename M> bool post_control(M &&m) const;

  /// Data post (midi_received/midi_to_wire) through the erased handle.
  bool post_data(data_message_t &&m) const;

private:
  std::shared_ptr<mailbox_base_t> mb_;
};

// ---------------------------------------------------------------------------
// Universal lifecycle messages (every actor accepts `stop`; every actor
// posts `stopped`/`actor_died` to its supervisor on exit).
// ---------------------------------------------------------------------------

/// Graceful stop: processed by the actor loop, runs stop hooks, exits.
struct stop_t {
  hdr_t hdr;
};
/// Posted by the actor wrapper after the loop exited (join is safe).
/// Carries the actor's own id so the router can match it to a pending
/// remove (stop choreography, design D7) or treat it as self-termination.
struct stopped_t {
  hdr_t hdr;
  peer_id_t peer_id = 0;
};
/// Fatal error: posted to the supervisor mailbox; loop exits.
struct actor_died_t {
  actor_id_t id = 0;
  std::string reason;
};
/// Router -> supervisor: delegate joining a wedged thread to the reaper.
struct reap_actor_t {
  std::jthread thread;
  std::string reason;
};
/// Topology change notification (router -> subscribers/partners).
enum class peer_event_kind_t : uint8_t {
  registered,
  removed,
  stopped,
  died,
  connected,
  disconnected,
};
struct peer_event_t {
  peer_event_kind_t kind = peer_event_kind_t::registered;
  peer_id_t peer_id = 0;
};
/// Generic typed-content carrier (notices, event streams, test messages).
struct control_payload_t {
  uint32_t tag = 0;
  std::string text;
};

// --- generic reply envelopes (any requester may receive these) -------------

/// Generic result ack.
struct ack_t {
  hdr_t hdr;
  bool ok = true;
  std::string error;
};
/// Assigned ids result for spawn/register requests. `mailbox` is filled by
/// the router for spawns so the caller (e.g. a listener routing datagrams
/// to its accepted peers) learns the spawned peer's mailbox.
struct peer_ids_result_t {
  hdr_t hdr;
  std::vector<peer_id_t> ids;
  mailbox_handle_t mailbox;
};

// --- core-only control variants --------------------------------------------

/// The main supervisor: exit notices, delegated reaps, stop-all acks.
using supervisor_control_t =
    std::variant<stop_t, stopped_t, actor_died_t, reap_actor_t, ack_t>;
/// Internal per-port reply mailboxes (e.g. ALSA register acks).
using reply_control_t = std::variant<peer_ids_result_t>;

// helpers to build typed control messages concisely (return the concrete
// struct; the posting side wraps it into its own control variant)
inline stop_t make_stop(hdr_t hdr = {}) { return stop_t{hdr}; }
inline stopped_t make_stopped(hdr_t hdr = {}, peer_id_t peer_id = 0) {
  return stopped_t{hdr, peer_id};
}
inline actor_died_t make_actor_died(actor_id_t id, std::string reason) {
  return actor_died_t{id, std::move(reason)};
}
inline peer_event_t make_peer_event(peer_event_kind_t kind, peer_id_t peer_id) {
  return peer_event_t{kind, peer_id};
}
inline control_payload_t make_control_payload(uint32_t tag,
                                              std::string text = {}) {
  return control_payload_t{tag, std::move(text)};
}

} // namespace rtpmididns

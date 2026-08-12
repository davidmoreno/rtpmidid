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

/// Actor message protocol (spec: actor-message-protocol; design D6/D7).
///
/// Every message owns its payload: crossing a queue is a copy or a move.
/// There are exactly two data-plane message types (`midi_received`,
/// `midi_to_wire`) on the data lane, and a small control-plane variant on
/// the control lane. Data-plane messages carry MIDI inline (no allocation
/// within capacity); oversized payloads use a bounded heap escape pool.

#pragma once

#include "peer_status.hpp"
#include "rtpmidid/logger.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>

namespace rtpmididns {

// The mailbox joins two lanes of the types below; the template is declared
// here and defined in mailbox.hpp. `mailbox_base_t` erases the lane types
// for cross-actor handles (messages carry `shared_ptr` handles so posting
// to a terminated actor's mailbox stays safe).
template <typename DataT, typename ControlT> class mailbox_t;
class mailbox_base_t;
class actor_base_t;
template <typename DataT, typename ControlT> class actor_t;

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

// ---------------------------------------------------------------------------
// Bounded heap escape pool for oversized MIDI payloads (design D6).
// Allocation is permitted up to a configurable byte budget; when the pool
// is exhausted the payload is dropped with a rate-limited log and the drop
// counter increments. Oversized MIDI is rare (rawmidi sysex floods), so
// this path stays off the hot path by construction.
// ---------------------------------------------------------------------------
class escape_pool_t {
public:
  static inline std::atomic<size_t> used_bytes{0};
  static inline std::atomic<uint64_t> dropped_payloads{0};
  static inline size_t limit = 4 * 1024 * 1024; // configured budget, 0 = unlimited

  static bool acquire(size_t n) {
    if (limit == 0) {
      used_bytes.fetch_add(n);
      return true;
    }
    size_t used = used_bytes.load();
    while (used + n <= limit) {
      if (used_bytes.compare_exchange_weak(used, used + n)) {
        return true;
      }
    }
    dropped_payloads.fetch_add(1);
    WARNING_RATE_LIMIT(5, "MIDI escape pool exhausted ({} bytes used, "
                          "{} bytes needed): oversized payload dropped.",
                       used, n);
    return false;
  }
  static void release(size_t n) { used_bytes.fetch_sub(n); }
};

/**
 * Owned MIDI payload: inline fixed storage (>= 1536 bytes, covering
 * MTU-sized RTP-MIDI packets) plus a bounded heap escape for oversized
 * payloads. Copying/moving across queues is self-owning: the payload stays
 * valid after the producer is destroyed.
 */
class midi_payload_t {
public:
  static constexpr size_t inline_capacity = 1536;

  midi_payload_t() = default;
  midi_payload_t(const midi_payload_t &other) : size_(other.size_) {
    if (other.heap_) {
      if (!escape_pool_t::acquire(other.size_)) {
        // Budget exhausted: copying is impossible; drop (the per-message
        // exception isolation in the actor handles this).
        throw std::runtime_error("midi escape pool exhausted on copy");
      }
      heap_ = std::make_unique<uint8_t[]>(other.size_);
      std::memcpy(heap_.get(), other.heap_.get(), other.size_);
    } else {
      inline_data_ = other.inline_data_;
    }
  }
  midi_payload_t &operator=(const midi_payload_t &other) {
    if (this != &other) {
      release_escape();
      size_ = other.size_;
      if (other.heap_) {
        if (!escape_pool_t::acquire(other.size_)) {
          throw std::runtime_error("midi escape pool exhausted on copy");
        }
        heap_ = std::make_unique<uint8_t[]>(other.size_);
        std::memcpy(heap_.get(), other.heap_.get(), other.size_);
      } else {
        inline_data_ = other.inline_data_;
      }
    }
    return *this;
  }
  midi_payload_t(midi_payload_t &&other) noexcept
      : inline_data_(std::move(other.inline_data_)), heap_(std::move(other.heap_)),
        size_(other.size_) {
    other.size_ = 0;
  }
  midi_payload_t &operator=(midi_payload_t &&other) noexcept {
    if (this != &other) {
      release_escape();
      inline_data_ = std::move(other.inline_data_);
      heap_ = std::move(other.heap_);
      size_ = other.size_;
      other.size_ = 0;
    }
    return *this;
  }
  ~midi_payload_t() { release_escape(); }

  /// Build a payload from a byte range. Payloads within the inline
  /// capacity never allocate. Oversized payloads use the heap escape pool;
  /// when the pool is exhausted the payload is dropped (nullopt).
  static std::optional<midi_payload_t> make(const uint8_t *data, size_t size) {
    midi_payload_t p;
    if (!p.assign(data, size)) {
      return std::nullopt;
    }
    return p;
  }

  bool assign(const uint8_t *data, size_t size) {
    release_escape();
    size_ = size;
    if (size <= inline_capacity) {
      std::memcpy(inline_data_.data(), data, size);
    } else {
      if (!escape_pool_t::acquire(size)) {
        size_ = 0;
        return false;
      }
      heap_ = std::make_unique<uint8_t[]>(size);
      std::memcpy(heap_.get(), data, size);
    }
    return true;
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  const uint8_t *data() const {
    return heap_ ? heap_.get() : inline_data_.data();
  }
  uint8_t *data() { return heap_ ? heap_.get() : inline_data_.data(); }

private:
  void release_escape() {
    if (heap_) {
      escape_pool_t::release(size_);
      heap_.reset();
    }
  }

  std::array<uint8_t, inline_capacity> inline_data_{};
  std::unique_ptr<uint8_t[]> heap_;
  size_t size_ = 0;
};

/**
 * Data-plane message: exactly two kinds (design D6), no variant overhead;
 * fixed-size with inline MIDI payload. `midi_received{from, payload}` goes
 * peer -> router; `midi_to_wire{to, from, payload}` goes router -> peer;
 * `to` lets one actor host multiple peer ids (e.g. ALSA ports).
 */
struct data_message_t {
  enum class kind_t : uint8_t { midi_received, midi_to_wire };

  kind_t kind = kind_t::midi_received;
  peer_id_t from = 0;
  peer_id_t to = 0;
  midi_payload_t payload;

  data_message_t() = default;

  static data_message_t midi_received(peer_id_t from, midi_payload_t &&payload) {
    data_message_t m;
    m.kind = kind_t::midi_received;
    m.from = from;
    m.payload = std::move(payload);
    return m;
  }
  static data_message_t midi_to_wire(peer_id_t to, peer_id_t from,
                                     midi_payload_t &&payload) {
    data_message_t m;
    m.kind = kind_t::midi_to_wire;
    m.to = to;
    m.from = from;
    m.payload = std::move(payload);
    return m;
  }
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
// Control-plane catalog (design D7/D9; extended per capability as the
// router, peers, worker and control socket are implemented).
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

// --- router control-plane catalog (spec: midi-routing, design D7) --------

/// Register a hosted peer id (e.g. an ALSA port) mapped to an existing
/// mailbox; the router assigns the id and acks it to the caller.
struct register_peer_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  mailbox_handle_t mailbox;
  std::string type; // for status ("alsa", ...)
  std::string meta; // extra info (name, port, ...)
};
/// Remove a hosted peer id without touching its hosting actor.
struct unregister_peer_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t peer_id = 0;
};
/// Spawn a standalone peer: the caller does all fallible preparation and
/// posts this prepared bundle; the router constructs the actor, spawns its
/// thread, registers its id, posts `registered{ids}` to the peer and acks
/// the caller (design D7 spawn flow).
struct spawn_peer_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  /// Prepared move-only bundle: the router assigns the peer id first and
  /// calls the factory with it, so the actor is born knowing its id (its
  /// `stopped`/`actor_died` carry it back to the router). Returns the
  /// actor through the type-erased base: peers of any family (network,
  /// rawmidi, ...) share the spawn path.
  std::move_only_function<std::shared_ptr<actor_base_t>(const mailbox_handle_t &,
                                                       peer_id_t)>
      factory;
  std::string type;
  std::string meta;
};
/// Router -> spawned peer: gate for wire traffic (design D7: a spawned
/// peer does not process wire traffic until it receives this).
struct registered_t {
  std::vector<peer_id_t> ids;
};
/// Remove: immediate topology cut, `stop`, await `stopped` under deadline,
/// join, ack (design D7 stop/remove choreography).
struct remove_peer_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t peer_id = 0;
};
struct connect_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t from = 0;
  peer_id_t to = 0;
};
struct disconnect_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t from = 0;
  peer_id_t to = 0;
};
/// Requester-driven status gather (design D7): the router answers with a
/// status head carrying router-assigned members and scatters
/// `peer_status_req{reply_to}` to each peer; peers answer the requester
/// directly.
struct peer_meta_t {
  peer_id_t id = 0;
  std::string type;
  std::vector<peer_id_t> send_to;
  peer_stats_t stats{0, 0};
};
struct status_req_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
struct status_head_t {
  hdr_t hdr;
  std::vector<peer_meta_t> peers;
  uint64_t router_data_drops = 0;
  uint64_t router_control_drops = 0;
};
struct peer_status_req_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t target = 0;
};
struct peer_status_resp_t {
  hdr_t hdr;
  peer_id_t peer_id = 0;
  peer_status_variant_t status;
};
/// Peer command relay (design D7): the router relays the request only; the
/// peer replies the typed result directly to the requester's `reply_to`.
struct peer_command_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t peer_id = 0;
  std::string cmd;
  std::string params_json;
};
struct peer_command_resp_t {
  hdr_t hdr;
  peer_id_t peer_id = 0;
  std::string result_json;
  bool is_error = false;
};
/// Topology event subscription (design D7): the router pushes `peer_event`
/// to the subscriber's mailbox for each topology change.
struct subscribe_events_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
struct unsubscribe_events_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
/// Bounded stop-all of spawned peers and ack (design D7 shutdown).
struct stop_all_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
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

// --- network listener <-> peer routing (design D2; tasks 5.2, 5.3) --------

/// RTP-MIDI port selector carried in routed datagrams.
enum class udp_port_e : uint8_t { control, midi };

/// Raw UDP datagram routed between network actors: a peer actor re-forwards
/// datagrams the SO_REUSEPORT hash misdelivered to it, and the listener
/// routes datagrams that landed on its accept sockets to the owning peer.
struct udp_datagram_t {
  udp_port_e port = udp_port_e::control;
  std::vector<uint8_t> data;
  std::string remote_ip; // numeric source address
  uint16_t remote_port = 0; // source port (control base)
};
/// Peer -> listener: the connection ended; drop the routing entries.
struct udp_peer_gone_t {
  uint32_t initiator_id = 0;
  uint32_t ssrc = 0;
};

// --- mdns actor (design D13; task 6.3) --------------------------------------

struct mdns_announcement_info_t {
  std::string name;
  int32_t port = 0;
};
struct mdns_remote_info_t {
  std::string name;
  std::string address;
  int32_t port = 0;
};
struct mdns_status_req_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
struct mdns_status_resp_t {
  hdr_t hdr;
  bool available = false;
  std::vector<mdns_announcement_info_t> announcements;
  std::vector<mdns_remote_info_t> remote_announcements;
};
/// Announce/unannounce/remove an rtpmidi service (control-socket users).
struct mdns_announce_t {
  std::string name;
  int32_t port = 0;
};
struct mdns_unannounce_t {
  std::string name;
  int32_t port = 0;
};
struct mdns_remove_t {
  std::string name;
  std::string hostname;
  int32_t port = 0;
};

// --- ALSA hosted-port creation (mdns discovery / network connections) -------

/// Ask the ALSA actor to create a seq port and register it as a hosted
/// peer; the reply carries the router-assigned id (peer_ids_result_t).
struct alsa_create_port_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  std::string name;
  std::string meta;
};
/// Ask the ALSA actor to remove a hosted port by router id.
struct alsa_remove_port_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t peer_id = 0;
};

// --- worker and DNS (design D10/D13) --------------------------------------

/// A blocking job for the worker actor (C++23 `std::move_only_function`;
/// small closures stored inline; the worker never knows result types).
struct worker_job_t {
  std::move_only_function<void()> job;
};
/// Typed DNS resolution result posted back to the requester.
struct dns_resolved_t {
  hdr_t hdr;
  std::string hostname;
  std::string port;
  std::vector<std::string> addresses; // empty = resolution failed
};

/// The transport control envelope: the union of every control message in
/// the system. It is the inter-actor wire format only — each actor's
/// mailbox control lane holds a narrow per-actor variant (see below) and
/// converts/validates on receipt, so an actor never sees a message class
/// it does not accept. Never pays for inline MIDI storage (design D6).
using control_message_t =
    std::variant<std::monostate, stop_t, stopped_t, actor_died_t, reap_actor_t,
                 peer_event_t, control_payload_t, register_peer_t,
                 unregister_peer_t, spawn_peer_t, registered_t, remove_peer_t,
                 connect_t, disconnect_t, status_req_t, status_head_t,
                 peer_status_req_t, peer_status_resp_t, peer_command_t,
                 peer_command_resp_t, subscribe_events_t, unsubscribe_events_t,
                 stop_all_t, ack_t, peer_ids_result_t, worker_job_t,
                 dns_resolved_t, udp_datagram_t, udp_peer_gone_t,
                 mdns_status_req_t, mdns_status_resp_t, mdns_announce_t,
                 mdns_unannounce_t, mdns_remove_t, alsa_create_port_t,
                 alsa_remove_port_t>;

// --- per-actor accepted control message sets -------------------------------
// Each actor declares exactly the control messages it accepts; its mailbox
// control lane element type IS this variant, so the mailbox only ever
// holds (and the actor only ever sees) its own messages.

/// The router: topology/peer lifecycle commands + peer exit notices.
using router_control_t =
    std::variant<stop_t, stopped_t, actor_died_t, register_peer_t,
                 unregister_peer_t, spawn_peer_t, remove_peer_t, connect_t,
                 disconnect_t, status_req_t, peer_command_t,
                 subscribe_events_t, unsubscribe_events_t, stop_all_t>;
/// Spawned/hosted peers: the registered gate, status/command requests,
/// topology events, routed datagrams (network peers) and DNS results.
using peer_control_t =
    std::variant<stop_t, registered_t, peer_status_req_t, peer_command_t,
                 peer_event_t, dns_resolved_t, udp_datagram_t>;
/// The mdns actor: status/announcement commands + the replies to its
/// create-port/spawn requests. No MIDI, no routing, nothing else.
using mdns_control_t =
    std::variant<stop_t, mdns_status_req_t, mdns_announce_t, mdns_unannounce_t,
                 mdns_remove_t, peer_ids_result_t, ack_t>;
/// The worker: blocking jobs only.
using worker_control_t = std::variant<stop_t, worker_job_t>;
/// The ALSA actor: hosted-port commands (its register acks land on small
/// internal reply mailboxes, not here).
using alsa_control_t =
    std::variant<stop_t, alsa_create_port_t, alsa_remove_port_t>;
/// A control-socket connection actor: the requester reply/event set.
using connection_control_t =
    std::variant<stop_t, ack_t, peer_ids_result_t, status_head_t,
                 peer_status_resp_t, peer_command_resp_t, mdns_status_resp_t,
                 dns_resolved_t, peer_event_t>;
/// A network listener: spawn acks, peer-gone notices, status requests,
/// and datagrams peers re-forward when the SO_REUSEPORT hash misdelivers
/// them (the listener demuxes and routes them to the owning peer).
using listener_control_t = std::variant<stop_t, peer_ids_result_t,
                                        udp_peer_gone_t, peer_status_req_t,
                                        udp_datagram_t>;
/// The control listener: connection exit notices.
using control_listener_control_t = std::variant<stop_t, stopped_t>;
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

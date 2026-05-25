/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "midipeer.hpp"
#include "rtpmidid/reply_channel.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

namespace rtpmididns {

class midirouter_t;

namespace router_cmd {

// ===========================================================================
// MIDI traffic (HIGH priority)
// ===========================================================================

/** Route MIDI from `from` to a single peer `to` (`to == 0` ⇒ broadcast). */
struct send_midi_t {
  peer_id_t from{0};
  peer_id_t to{0};
  std::vector<uint8_t> data;
};

// ===========================================================================
// Topology mutations (NORMAL priority)
// ===========================================================================

/** Add `peer` to the router; reply carries the assigned `peer_id_t`. */
struct add_peer_t {
  std::shared_ptr<midipeer_t> peer;
  rtpmidid::reply_slot_t reply;
};

struct remove_peer_t {
  peer_id_t id{0};
};

/** Remove every peer (no per-peer reply; one final reply when the loop ends). */
struct remove_all_peers_t {
  rtpmidid::reply_slot_t reply;
};

struct connect_t {
  peer_id_t from{0};
  peer_id_t to{0};
};

struct disconnect_t {
  peer_id_t from{0};
  peer_id_t to{0};
};

/** Directed peer event from `from` to `to`. */
struct event_directed_t {
  peer_id_t from{0};
  peer_id_t to{0};
  midipeer_event_e evt{midipeer_event_e::CONNECTED_ROUTER};
};

/** Broadcast peer event from `from` to all of its current send targets. */
struct event_broadcast_t {
  peer_id_t from{0};
  midipeer_event_e evt{midipeer_event_e::CONNECTED_ROUTER};
};

// ===========================================================================
// Signal dispatch (NORMAL priority)
// ===========================================================================
// Replaces the old `defer_router_callback` / `poller.call_later` mechanism.
// Each signal has its own typed message so listeners can be reasoned about
// statically.

struct signal_peer_added_t {
  peer_id_t peer_id{0};
};

struct signal_connected_t {
  peer_id_t from{0};
  peer_id_t to{0};
};

struct signal_disconnected_t {
  peer_id_t from{0};
  peer_id_t to{0};
};

struct signal_peer_event_t {
  peer_id_t peer_id{0};
  midipeer_event_e evt{midipeer_event_e::CONNECTED_ROUTER};
};

// ===========================================================================
// Read APIs (LOW priority) — each returns a typed value via std::any payload
// ===========================================================================

struct query_peer_count_t {
  rtpmidid::reply_slot_t reply;
};

struct query_peer_ids_t {
  rtpmidid::reply_slot_t reply;
};

struct query_send_targets_t {
  peer_id_t from{0};
  rtpmidid::reply_slot_t reply;
};

struct query_get_peer_t {
  peer_id_t peer_id{0};
  rtpmidid::reply_slot_t reply;
};

struct query_status_rows_t {
  rtpmidid::reply_slot_t reply;
};

// ===========================================================================
// Iteration (LOW priority) — carries a user lambda but with a semantic name
// ===========================================================================

/** Visit every peer (downcast performed by the caller-provided lambda). */
struct for_each_peer_t {
  std::function<void(midirouter_t &)> task;
  rtpmidid::reply_slot_t reply;
};

/** Visit every peer that `peer_id` currently sends to. */
struct peer_connection_loop_t {
  peer_id_t peer_id{0};
  std::function<void(std::shared_ptr<midipeer_t>)> func;
  rtpmidid::reply_slot_t reply;
};

// ===========================================================================
// Lifecycle
// ===========================================================================

/** Tell the router thread to exit (handler flips `router_running_`). */
struct shutdown_t {};

} // namespace router_cmd

using router_command_t =
    std::variant<router_cmd::send_midi_t, router_cmd::add_peer_t,
                 router_cmd::remove_peer_t, router_cmd::remove_all_peers_t,
                 router_cmd::connect_t, router_cmd::disconnect_t,
                 router_cmd::event_directed_t, router_cmd::event_broadcast_t,
                 router_cmd::signal_peer_added_t,
                 router_cmd::signal_connected_t,
                 router_cmd::signal_disconnected_t,
                 router_cmd::signal_peer_event_t,
                 router_cmd::query_peer_count_t, router_cmd::query_peer_ids_t,
                 router_cmd::query_send_targets_t,
                 router_cmd::query_get_peer_t,
                 router_cmd::query_status_rows_t,
                 router_cmd::for_each_peer_t,
                 router_cmd::peer_connection_loop_t, router_cmd::shutdown_t>;

} // namespace rtpmididns

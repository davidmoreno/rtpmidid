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

/** Route MIDI from `from` to a single peer `to` (`to == 0` ⇒ broadcast). */
struct send_midi_t {
  peer_id_t from{0};
  peer_id_t to{0};
  std::vector<uint8_t> data;
};

/** Add `peer` to the router; reply carries the assigned `peer_id_t`. */
struct add_peer_t {
  std::shared_ptr<midipeer_t> peer;
  rtpmidid::reply_slot_t reply;
};

struct remove_peer_t {
  peer_id_t id{0};
};

struct connect_t {
  peer_id_t from{0};
  peer_id_t to{0};
};

struct disconnect_t {
  peer_id_t from{0};
  peer_id_t to{0};
};

/** Peer event broadcast (`to == 0`) or directed (`to != 0`). */
struct event_t {
  peer_id_t from{0};
  peer_id_t to{0};
  midipeer_event_e evt{midipeer_event_e::CONNECTED_ROUTER};
};

/**
 * Fire a router signal (or post-mutation hook) on the router thread.
 *
 * Replaces the old `defer_router_callback` / `poller.call_later` mechanism.
 * The task is invoked with a reference to the router so it can fire signals
 * directly.
 */
struct fire_signal_t {
  std::function<void(midirouter_t &)> task;
};

/**
 * Run an arbitrary action on the router thread. Optional reply slot (used by
 * read-only helpers like `for_each_peer`, `peer_connection_loop`).
 */
struct run_task_t {
  std::function<void(midirouter_t &)> task;
  rtpmidid::reply_slot_t reply;
};

/**
 * Read state on the router thread; the returned `std::any` is delivered into
 * `reply.channel` so the caller can recover a typed value.
 */
struct query_t {
  std::function<std::any(midirouter_t &)> query;
  rtpmidid::reply_slot_t reply;
};

/** Wake the router loop so it observes `router_running == false`. */
struct shutdown_t {};

} // namespace router_cmd

using router_command_t =
    std::variant<router_cmd::send_midi_t, router_cmd::add_peer_t,
                 router_cmd::remove_peer_t, router_cmd::connect_t,
                 router_cmd::disconnect_t, router_cmd::event_t,
                 router_cmd::fire_signal_t, router_cmd::run_task_t,
                 router_cmd::query_t, router_cmd::shutdown_t>;

} // namespace rtpmididns

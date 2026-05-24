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

#include "rtpmidid/reply_channel.hpp"
#include "rtpmidid/threading_types.hpp"
#include <functional>
#include <variant>

namespace rtpmididns {

class midipeer_t;

namespace peer_cmd {

/** MIDI packet inbound to this peer (delivered by the router). */
struct process_midi_t {
  rtpmidid::midi_packet_t packet;
};

/** MIDI packet that this peer wants to send back to the router. */
struct send_to_router_t {
  rtpmidid::midi_packet_t packet;
};

/**
 * Run an arbitrary action on the peer thread (e.g. status snapshot, latency
 * read). Optional reply slot for read-back.
 */
struct run_task_t {
  std::function<void(midipeer_t &)> task;
  rtpmidid::reply_slot_t reply;
};

/**
 * Read state on the peer thread; result delivered as `std::any` into
 * `reply.channel`.
 */
struct query_t {
  std::function<std::any(midipeer_t &)> query;
  rtpmidid::reply_slot_t reply;
};

struct shutdown_t {};

} // namespace peer_cmd

using peer_command_t =
    std::variant<peer_cmd::process_midi_t, peer_cmd::send_to_router_t,
                 peer_cmd::run_task_t, peer_cmd::query_t, peer_cmd::shutdown_t>;

} // namespace rtpmididns

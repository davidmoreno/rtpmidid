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
#include <variant>

namespace rtpmididns {

class midipeer_t;

namespace peer_cmd {

/** MIDI packet inbound to this peer (delivered by the router). HIGH priority. */
struct process_midi_t {
  rtpmidid::midi_packet_t packet;
};

/** Snapshot the peer's internal latency stats. LOW priority. */
struct query_internal_latency_stats_t {
  rtpmidid::reply_slot_t reply;
};

/** Wake the loop so it observes `thread_running_ == false`. */
struct shutdown_t {};

} // namespace peer_cmd

using peer_command_t =
    std::variant<peer_cmd::process_midi_t,
                 peer_cmd::query_internal_latency_stats_t,
                 peer_cmd::shutdown_t>;

} // namespace rtpmididns

/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once
#include "poller.hpp"
#include "rtppeer.hpp"
#include <chrono>

namespace rtpmidid {

/**
 * @short Keeps the recovery journal conversation with a peer going.
 *
 * Two jobs, both on the poller thread:
 *
 * - **Receiver feedback**: periodically tell the peer the highest sequence
 *   number we have received ('RS' packet on the control port), so it can shrink
 *   its checkpoint history and stop sending guard packets (the closed-loop
 *   sending policy).
 * - **Guard packets**: while we have journal state the peer may not have
 *   confirmed yet, keep sending journal-only packets, backing off from
 *   @c guard_min_period to @c guard_max_period. A NoteOff that never arrives
 *   because the stream went quiet is exactly the stuck note the journal is for,
 *   and this is how RFC 4696 Section 4.2 recommends guarding against it. The
 *   guards stop once the peer's feedback catches up, or while real MIDI is
 *   flowing.
 *
 * One instance per peer, created by the client and server peer classes. It
 * starts and stops itself following the peer status.
 */
class journal_timer_t {
  NON_COPYABLE_NOR_MOVABLE(journal_timer_t);

public:
  /// Resolution at which the periods below are evaluated.
  std::chrono::milliseconds tick_period{100};
  /// How often to send 'RS' receiver feedback while connected.
  std::chrono::milliseconds feedback_period{1000};
  /// First guard packet delay after the last MIDI packet.
  std::chrono::milliseconds guard_min_period{100};
  /// Longest guard packet delay (RFC 4696 guardtime).
  std::chrono::milliseconds guard_max_period{1000};

  explicit journal_timer_t(rtppeer_t &peer);
  ~journal_timer_t();

  /// True while the periodic tick is armed.
  bool is_running() const { return scheduled; }

private:
  void schedule();
  void unschedule();
  void tick();
  void on_status(rtppeer_t::status_e status);
  void on_midi_sent();

  rtppeer_t &peer;
  poller_t::timer_t timer;
  rtppeer_t::status_change_event_t::connection_t status_connection;
  rtppeer_t::midi_sent_event_t::connection_t midi_sent_connection;
  std::chrono::steady_clock::time_point last_feedback{};
  std::chrono::steady_clock::time_point last_activity{};
  std::chrono::steady_clock::time_point last_guard{};
  std::chrono::milliseconds guard_period{100};
  bool scheduled = false;
  bool ever_ticked = false;
  /// Extended sequence number of the first guard packet of the episode in
  /// progress. Its journal is the one that carries the pending note state, so
  /// the episode is done as soon as the peer's feedback covers it; the later
  /// guards are retransmissions of the same content (RFC 4696 §4.2). Valid
  /// while @c has_guard_anchor is true, which is also what keeps the DEBUG line
  /// in tick() to one per episode.
  uint32_t guard_anchor = 0;
  bool has_guard_anchor = false;
};

} // namespace rtpmidid

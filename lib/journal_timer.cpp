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

#include <rtpmidid/journal_timer.hpp>
#include <rtpmidid/logger.hpp>

using namespace rtpmidid;

journal_timer_t::journal_timer_t(rtppeer_t &peer_) : peer(peer_) {
  guard_period = guard_min_period;
  status_connection = peer.status_change_event.connect(
      [this](rtppeer_t::status_e status) { on_status(status); });
  midi_sent_connection =
      peer.midi_sent_event.connect([this]() { on_midi_sent(); });

  if (peer.is_connected()) {
    on_status(rtppeer_t::status_e::CONNECTED);
  }
}

journal_timer_t::~journal_timer_t() { unschedule(); }

void journal_timer_t::schedule() {
  if (scheduled) {
    return;
  }
  scheduled = true;
  timer = poller.add_timer_event(tick_period, [this]() { tick(); });
}

void journal_timer_t::unschedule() {
  scheduled = false;
  timer.disable();
}

void journal_timer_t::on_status(rtppeer_t::status_e status) {
  if (status == rtppeer_t::status_e::CONNECTED) {
    // The first tick sends the first feedback: the peer needs it as soon as
    // there is anything to report.
    last_feedback = {};
    last_guard = {};
    last_activity = std::chrono::steady_clock::now();
    guard_period = guard_min_period;
    ever_ticked = false;
    guarding = false;
    schedule();
    return;
  }
  if (rtppeer_t::is_disconnected(status)) {
    unschedule();
  }
}

void journal_timer_t::on_midi_sent() {
  // Real MIDI is flowing: no guard packet is needed now, and the backoff
  // restarts from the minimum delay.
  last_activity = std::chrono::steady_clock::now();
  guard_period = guard_min_period;
  // Real MIDI restarts the guard episode: when it goes quiet again, the DEBUG
  // line below reports it afresh.
  guarding = false;
}

void journal_timer_t::tick() {
  scheduled = false;
  if (!peer.is_connected() || !peer.recovery_journal.enabled) {
    return;
  }
  schedule(); // Re-arm first: a throw below must not stop the timer

  auto now = std::chrono::steady_clock::now();
  auto &journal = peer.recovery_journal;

  // Receiver feedback: tells the peer which packets we have, so it can shrink
  // its journal and stop guarding.
  if (journal.has_received_packet() &&
      (!ever_ticked || now - last_feedback >= feedback_period)) {
    ever_ticked = true;
    last_feedback = now;
    peer.send_feedback();
  }

  // Guard packets: only while there is state the peer has not confirmed.
  if (!journal.sender_has_pending_state() || journal.sender_is_caught_up()) {
    guard_period = guard_min_period;
    guarding = false;
    return;
  }
  if (now - last_activity < guard_min_period) {
    return; // MIDI activity, the stream can speak for itself
  }
  if (last_guard != std::chrono::steady_clock::time_point{} &&
      now - last_guard < guard_period) {
    return;
  }
  if (peer.send_journal_packet()) {
    if (!guarding) {
      // Logged once per guard episode: the packet is retransmitted with backoff
      // until the peer confirms, so logging each one would only repeat itself.
      guarding = true;
      if (journal.has_feedback()) {
        DEBUG("Peer {} has not confirmed the journal yet, resending it: {} "
              "notes pending (it confirmed #{}, we sent up to #{})",
              peer.remote_name, journal.sender_pending_notes(),
              journal.confirmed_extended_seq(),
              journal.last_sent_extended_seq());
      } else {
        DEBUG("Peer {} has sent no journal feedback, resending it: {} notes "
              "pending (we sent up to #{})",
              peer.remote_name, journal.sender_pending_notes(),
              journal.last_sent_extended_seq());
      }
    }
    last_guard = now;
    guard_period = std::min(guard_period * 2, guard_max_period);
  }
}

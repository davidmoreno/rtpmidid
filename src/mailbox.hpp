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

#pragma once

#include "messages.hpp"
#include "queue.hpp"
#include "rtpmidid/logger.hpp"
#include <memory>

namespace rtpmididns {

// The concrete actor mailbox joins the protocol's two lanes:
// using actor_mailbox_t = mailbox_t<data_message_t, control_message_t>;
// (declared in messages.hpp)

/**
 * Central lane-capacity constants (design D3): the single place to tune
 * lane capacities system-wide, validated by the cutover measurements
 * (tasks 8.4-8.5). Both lanes default to drop_oldest so the freshest data
 * and the newest control message survive a flood.
 */
inline constexpr size_t mailbox_data_capacity = 4096;
inline constexpr size_t mailbox_control_capacity = 256;
inline constexpr drop_policy_t mailbox_data_drop_policy = drop_policy_t::drop_oldest;
inline constexpr drop_policy_t mailbox_control_drop_policy = drop_policy_t::drop_oldest;

/**
 * The mailbox: joins N lanes under ONE shared wake source (one eventfd
 * doorbell) so a successful enqueue on any lane arms the same doorbell and
 * a burst of enqueues costs exactly one eventfd write (design D3/D14).
 *
 * v1 joins exactly two lanes: a data lane and a control lane, both bounded
 * `mpsc_queue_t`s constructed with the shared waker. The lane element types
 * are template parameters so the wake/drain machinery is testable with
 * trivial types and the concrete protocol messages (actor-message-protocol)
 * plug in as `mailbox_t<data_message_t, control_message_t>`.
 *
 * Producer API (any thread): post_data / post_control.
 * Consumer API (actor thread only): prepare / pop_data / pop_control /
 * pop_matching / idle / drain_data_first.
 *
 * Instances live in `std::shared_ptr<mailbox_t<...>>`: the owning actor
 * holds one reference and control-plane messages carry the others as
 * handles, so posting to a mailbox whose actor has terminated stays safe.
 */
template <typename DataT, typename ControlT> class mailbox_t {
public:
  using data_message_type = DataT;
  using control_message_type = ControlT;

  mailbox_t(size_t data_capacity = mailbox_data_capacity,
            size_t control_capacity = mailbox_control_capacity)
      : data_(waker_, data_capacity, mailbox_data_drop_policy),
        control_(waker_, control_capacity, mailbox_control_drop_policy) {}

  /// The only fd the actor registers in its poller.
  int doorbell_fd() const { return waker_.fd(); }

  // --- producer API (any thread) ---

  /// Returns whether the message was enqueued or dropped.
  bool post_data(DataT &&msg) { return data_.push(std::move(msg)); }
  bool post_data(const DataT &msg) { return data_.push(msg); }
  /// Returns whether the message was enqueued or dropped. A full control
  /// lane is near-fatal: control messages are rare and some are load-bearing.
  bool post_control(ControlT &&msg) {
    const auto drops_before = control_.drops();
    const bool enqueued = control_.push(std::move(msg));
    if (!enqueued || control_.drops() != drops_before) {
      WARNING_RATE_LIMIT(5, "Control lane full: a control message was dropped "
                            "or displaced. This is near-fatal; check for a "
                            "stalled or flooded actor.");
    }
    return enqueued;
  }
  bool post_control(const ControlT &msg) {
    const auto drops_before = control_.drops();
    const bool enqueued = control_.push(msg);
    if (!enqueued || control_.drops() != drops_before) {
      WARNING_RATE_LIMIT(5, "Control lane full: a control message was dropped "
                            "or displaced. This is near-fatal; check for a "
                            "stalled or flooded actor.");
    }
    return enqueued;
  }

  // --- consumer API (actor thread only) ---

  /// Read the doorbell and clear the coalescing flag before draining.
  void prepare() { waker_.prepare(); }

  std::optional<DataT> pop_data() { return data_.try_pop(); }
  std::optional<ControlT> pop_control() { return control_.try_pop(); }

  /**
   * Selective control pop (design D15 affordance): scans the control lane
   * and consumes exactly the first message matching the predicate; all
   * non-matching messages stay queued in their original order.
   */
  template <typename Pred> std::optional<ControlT> pop_matching(Pred &&pred) {
    return control_.try_pop_matching(std::forward<Pred>(pred));
  }

  /// True when both lanes are empty (used by the no-lost-wakeup re-check).
  bool idle() const { return data_.empty() && control_.empty(); }

  /**
   * Default drain policy (design D4): drain the data lane completely (what
   * is queued now, never waiting for more), then process exactly one
   * control message, and repeat until the control lane is empty. Neither
   * lane can starve the other; a control burst delays data processing by
   * at most one control message per pass.
   */
  template <typename OnData, typename OnControl>
  void drain_data_first(OnData &&on_data, OnControl &&on_control) {
    do {
      data_.drain(std::forward<OnData>(on_data));
      if (auto c = control_.try_pop()) {
        on_control(std::move(*c));
      }
    } while (!control_.empty());
  }

  /// Aggregate drop counters, for status reporting.
  uint64_t data_drops() const { return data_.drops(); }
  uint64_t control_drops() const { return control_.drops(); }

  /// Drain the data lane completely through a handler.
  template <typename OnData> void drain_data(OnData &&on_data) {
    data_.drain(std::forward<OnData>(on_data));
  }

private:
  eventfd_waker_t waker_; // declared first: the lanes bind to it
  mpsc_queue_t<DataT> data_;
  mpsc_queue_t<ControlT> control_;
};

} // namespace rtpmididns

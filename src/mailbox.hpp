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

/// The mailbox: the queue layer of the actor runtime.
///
/// `mailbox_t<DataT, ControlT>` joins a data lane and a control lane under
/// one eventfd doorbell. The control lane element type IS the actor's own
/// control variant, so a mailbox only ever holds — and the actor only ever
/// sees — the messages it accepts. There is deliberately NO global message
/// catalog: cross-actor posting through the type-erased `mailbox_handle_t`
/// carries the message in a small `control_message_box_t` (type identity +
/// owned storage) and the target mailbox matches it against its own
/// variant.

#pragma once

#include "data_message.hpp"
#include "message_core.hpp"
#include "queue.hpp"
#include "rtpmidid/logger.hpp"
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <utility>

namespace rtpmididns {

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
 * Type-erased control message carrier: holds one concrete control message
 * of any type, identified by its `typeid`. The target mailbox matches it
 * against the alternatives of its own control variant (no global message
 * catalog exists), so posting a message an actor does not accept is
 * rejected at runtime with a warning. Control messages are rare and may
 * allocate freely (design D6).
 */
class control_message_box_t {
public:
  template <typename M>
  control_message_box_t(M &&m)
      : type_(typeid(std::decay_t<M>)),
        data_(new std::decay_t<M>(std::forward<M>(m)),
              [](void *p) { delete static_cast<std::decay_t<M> *>(p); }) {}

  std::type_index type() const { return type_; }
  void *data() { return data_.get(); }

private:
  std::type_index type_;
  std::unique_ptr<void, void (*)(void *)> data_;
};

/**
 * Type-erased mailbox interface (design D6): lets cross-actor handles post
 * messages to any mailbox. The concrete `mailbox_t` validates each message
 * against its own accepted lane types and drops (with a warning) anything
 * it does not accept.
 */
class mailbox_base_t {
public:
  virtual ~mailbox_base_t() = default;
  /// Post a data message (midi_received/midi_to_wire); false if this
  /// mailbox has no data lane for it.
  virtual bool post_data(data_message_t &&msg) = 0;
  /// Post a type-erased control message; false if this mailbox's control
  /// lane does not accept that message type.
  virtual bool post_transport(control_message_box_t &&box) = 0;
};

// Definitions of the type-erased handle members (the message box and the
// mailbox base are complete here).
template <typename M>
bool mailbox_handle_t::post_control(M &&m) const {
  if (!mb_) {
    return false;
  }
  return mb_->post_transport(control_message_box_t{std::forward<M>(m)});
}
inline bool mailbox_handle_t::post_data(data_message_t &&m) const {
  return mb_ ? mb_->post_data(std::move(m)) : false;
}

/**
 * The mailbox: joins N lanes under ONE shared wake source (one eventfd
 * doorbell) so a successful enqueue on any lane arms the same doorbell and
 * a burst of enqueues costs exactly one eventfd write (design D3/D14).
 *
 * v1 joins exactly two lanes: a data lane and a control lane, both bounded
 * `mpsc_queue_t`s constructed with the shared waker. The lane element types
 * are template parameters: each actor instantiates the mailbox with its own
 * accepted data type (usually `data_message_t`, or `std::monostate` when it
 * has no data lane) and its own control variant (only the control messages
 * it accepts). The typed `post_control<M>`/`post_data<M>` only compile for
 * accepted message types, so the type system enforces the per-actor
 * acceptance sets; the erased path (`post_transport`) validates at runtime.
 *
 * Producer API (any thread): post_data / post_control.
 * Consumer API (actor thread only): prepare / pop_data / pop_control /
 * pop_matching / idle / drain_data_first.
 *
 * Instances live in `std::shared_ptr<mailbox_t<...>>`: the owning actor
 * holds one reference and control-plane messages carry the others as
 * handles, so posting to a mailbox whose actor has terminated stays safe.
 */
template <typename DataT, typename ControlT> class mailbox_t : public mailbox_base_t {
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

  /// Typed data post: only compiles for this mailbox's data lane type.
  template <typename M> bool post_data(M &&msg) {
    static_assert(std::is_same_v<std::decay_t<M>, DataT>,
                  "this mailbox does not accept that data message type");
    return data_.push(std::forward<M>(msg));
  }
  /// Typed control post: only compiles for messages this actor accepts.
  template <typename M> bool post_control(M &&msg) {
    static_assert(is_alternative_v<std::decay_t<M>, ControlT>,
                  "this mailbox does not accept that control message type");
    return post_control_impl(ControlT{std::forward<M>(msg)});
  }
  /// Returns whether the message was enqueued or dropped. A full control
  /// lane is near-fatal: control messages are rare and some are load-bearing.
  bool post_control(ControlT &&msg) { return post_control_impl(std::move(msg)); }

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

  // --- erased producer API (via mailbox_base_t) ---

  /// Data post through the base: only mailboxes with a data lane accept it.
  bool post_data(data_message_t &&msg) override {
    if constexpr (std::is_same_v<DataT, data_message_t>) {
      return data_.push(std::move(msg));
    } else {
      return false; // this mailbox has no data lane
    }
  }

  /// Type-erased control post through the base: matches the message's type
  /// identity against this mailbox's own control variant, accepting only
  /// the messages in the actor's declared set (a misdirected message is a
  /// wiring bug: it is dropped and logged).
  bool post_transport(control_message_box_t &&box) override {
    if constexpr (is_variant_v<ControlT>) {
      const auto type = box.type();
      bool ok = false;
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((ok = ok || insert_alternative<I>(type, box)), ...);
      }(std::make_index_sequence<std::variant_size_v<ControlT>>());
      if (!ok) {
        WARNING_RATE_LIMIT(5, "Mailbox: dropped a control message type it "
                              "does not accept (wiring bug).");
      }
      return ok;
    } else {
      return false; // non-variant control lane: no erased posts
    }
  }

private:
  template <std::size_t I>
  bool insert_alternative(const std::type_index &type,
                          control_message_box_t &box) {
    using T = std::variant_alternative_t<I, ControlT>;
    if (type == typeid(T)) {
      return post_control_impl(ControlT{std::move(*static_cast<T *>(box.data()))});
    }
    return false;
  }

  bool post_control_impl(ControlT &&msg) {
    const auto drops_before = control_.drops();
    const bool enqueued = control_.push(std::move(msg));
    if (!enqueued || control_.drops() != drops_before) {
      WARNING_RATE_LIMIT(5, "Control lane full: a control message was dropped "
                            "or displaced. This is near-fatal; check for a "
                            "stalled or flooded actor.");
    }
    return enqueued;
  }

  eventfd_waker_t waker_; // declared first: the lanes bind to it
  mpsc_queue_t<DataT> data_;
  mpsc_queue_t<ControlT> control_;
};

// --- concrete mailbox types for the core control variants ------------------
// (The per-actor mailbox aliases live next to their control variants in the
// subsystem message headers: router_mailbox_t in router_messages.hpp, ...)
using supervisor_mailbox_t = mailbox_t<std::monostate, supervisor_control_t>;
using reply_mailbox_t = mailbox_t<std::monostate, reply_control_t>;

} // namespace rtpmididns

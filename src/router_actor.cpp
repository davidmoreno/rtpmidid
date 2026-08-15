/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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

#include "router_actor.hpp"
#include "rtpmidid/logger.hpp"
#include <algorithm>

namespace rtpmididns {

router_actor_t::router_actor_t(actor_config_t config)
    : actor_t(std::move(config)) {}

void router_actor_t::on_data(data_message_t &&msg) {
  if (msg.kind != data_message_t::kind_t::midi_received) {
    WARNING("Router: unexpected data message kind; dropped.");
    return;
  }
  forward_midi(std::move(msg));
}

void router_actor_t::forward_midi(data_message_t &&msg) {
  auto from_it = peers_.find(msg.from);
  if (from_it == peers_.end()) {
    WARNING_RATE_LIMIT(5, "Router: midi_received from unknown sender={} "
                          "dropped.",
                       msg.from);
    return;
  }
  auto &from = from_it->second;
  from.stats.sent++;
  const auto n = from.send_to.size();
  for (size_t i = 0; i < n; i++) {
    const auto to_id = from.send_to[i];
    auto dest = peers_.find(to_id);
    if (dest == peers_.end()) {
      // The graph is the router's own map; a missing destination is an
      // inconsistency (stale edge). Loud, rate-limited.
      WARNING_RATE_LIMIT(5, "Router: forwarding from={} to missing peer={} "
                            "(stale graph edge); message dropped.",
                         msg.from, to_id);
      continue;
    }
    dest->second.stats.recv++;
    try {
      if (i + 1 == n) {
        // N-1 copies + 1 move (design D2); inline payloads never allocate.
        dest->second.mailbox.post_data(
            data_message_t::midi_to_wire(to_id, msg.from,
                                         std::move(msg.payload)));
      } else {
        dest->second.mailbox.post_data(
            data_message_t::midi_to_wire(to_id, msg.from,
                                         midi_payload_t(msg.payload)));
      }
    } catch (const std::exception &e) {
      // Oversized-payload escape pool exhausted on copy: drop this
      // destination (per-message isolation).
      ERROR("Router: forwarding to={} failed error={}", to_id, quoted_t{e.what()});
    }
  }
}

void router_actor_t::on_control(router_control_t &&msg) {
  std::visit(
      [this](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, register_peer_t>) {
          handle_register_peer(std::move(m));
        } else if constexpr (std::is_same_v<T, unregister_peer_t>) {
          handle_unregister_peer(std::move(m));
        } else if constexpr (std::is_same_v<T, spawn_peer_t>) {
          handle_spawn_peer(std::move(m));
        } else if constexpr (std::is_same_v<T, remove_peer_t>) {
          handle_remove_peer(std::move(m));
        } else if constexpr (std::is_same_v<T, connect_t>) {
          handle_connect(std::move(m));
        } else if constexpr (std::is_same_v<T, disconnect_t>) {
          handle_disconnect(std::move(m));
        } else if constexpr (std::is_same_v<T, status_req_t>) {
          handle_status_req(std::move(m));
        } else if constexpr (std::is_same_v<T, peer_command_t>) {
          handle_peer_command(std::move(m));
        } else if constexpr (std::is_same_v<T, subscribe_events_t>) {
          handle_subscribe_events(std::move(m));
        } else if constexpr (std::is_same_v<T, unsubscribe_events_t>) {
          handle_unsubscribe_events(std::move(m));
        } else if constexpr (std::is_same_v<T, stop_all_t>) {
          handle_stop_all(std::move(m));
        } else if constexpr (std::is_same_v<T, stopped_t>) {
          handle_stopped(std::move(m));
        } else if constexpr (std::is_same_v<T, actor_died_t>) {
          handle_actor_died(std::move(m));
        }
      },
      msg);
}

void router_actor_t::on_loop() { check_pending_removes(); }

// ---------------------------------------------------------------------------
// Lifecycle handlers
// ---------------------------------------------------------------------------

void router_actor_t::handle_register_peer(register_peer_t &&m) {
  if (!m.mailbox) {
    ack(m.reply_to, m.hdr, false, "register_peer without a mailbox");
    return;
  }
  const auto id = max_id_++;
  peer_record_t rec;
  rec.mailbox = std::move(m.mailbox);
  rec.type = std::move(m.type);
  rec.meta = std::move(m.meta);
  peers_[id] = std::move(rec);
  INFO("Router: registered hosted peer id={} type={}", id,
       quoted_t{peers_[id].type.empty() ? "?" : peers_[id].type});
  notify_subscribers(peer_event_t{peer_event_kind_t::registered, id});
  m.reply_to.post_control(
      peer_ids_result_t{m.hdr, {id}, peers_[id].mailbox});
}

void router_actor_t::handle_unregister_peer(unregister_peer_t &&m) {
  auto it = peers_.find(m.peer_id);
  if (it == peers_.end()) {
    ack(m.reply_to, m.hdr, false, "unregister: unknown peer");
    return;
  }
  if (it->second.thread.has_value()) {
    ack(m.reply_to, m.hdr, false,
        "unregister: peer is spawned; use remove_peer");
    return;
  }
  INFO("Router: unregistered hosted peer id={}", m.peer_id);
  erase_peer(m.peer_id, peer_event_kind_t::removed);
  ack(m.reply_to, m.hdr, true);
}

void router_actor_t::handle_spawn_peer(spawn_peer_t &&m) {
  if (!m.factory) {
    ack(m.reply_to, m.hdr, false, "spawn_peer without a factory");
    return;
  }
  const auto id = max_id_++;
  std::shared_ptr<actor_base_t> peer;
  try {
    // The router mailbox is the peer's supervisor (stopped/actor_died land
    // back here). The caller has done all fallible preparation; the id is
    // assigned before construction so the actor posts it back on exit.
    peer = m.factory(mailbox_handle(), id);
  } catch (const std::exception &e) {
    ack(m.reply_to, m.hdr, false,
        std::string("spawn_peer preparation failed: ") + e.what());
    return;
  }
  if (!peer) {
    ack(m.reply_to, m.hdr, false, "spawn_peer factory returned null");
    return;
  }
  peer_record_t rec;
  rec.mailbox = peer->mailbox_handle();
  rec.actor = peer;
  peer->start();
  rec.thread = peer->take_thread(); // the router owns the thread (D9)
  rec.type = std::move(m.type);
  rec.meta = std::move(m.meta);
  peers_[id] = std::move(rec);
  INFO("Router: spawned peer id={} type={}", id,
       quoted_t{peers_[id].type.empty() ? "?" : peers_[id].type});
  // Gate: the peer waits for `registered` before handling wire traffic.
  peers_[id].mailbox.post_control(registered_t{{id}});
  m.reply_to.post_control(
      peer_ids_result_t{m.hdr, {id}, peers_[id].mailbox});
}

void router_actor_t::handle_remove_peer(remove_peer_t &&m) {
  auto it = peers_.find(m.peer_id);
  if (it == peers_.end()) {
    ack(m.reply_to, m.hdr, false, "remove: unknown peer");
    return;
  }
  // Immediate topology cut, then the stop/stopped choreography (D7).
  cut_topology(m.peer_id);
  if (!it->second.thread.has_value()) {
    // Hosted id: nothing to stop; complete immediately.
    erase_peer(m.peer_id, peer_event_kind_t::removed);
    ack(m.reply_to, m.hdr, true);
    return;
  }
  pending_removes_[m.peer_id] =
      pending_remove_t{m.hdr, m.reply_to,
                       std::chrono::steady_clock::now() + remove_deadline_,
                       false};
  it->second.mailbox.post_control(stop_t{m.hdr});
}

void router_actor_t::handle_connect(connect_t &&m) {
  auto from = peers_.find(m.from);
  auto to = peers_.find(m.to);
  if (from == peers_.end() || to == peers_.end()) {
    ack(m.reply_to, m.hdr, false, "connect: unknown peer");
    return;
  }
  auto &send_to = from->second.send_to;
  if (std::find(send_to.begin(), send_to.end(), m.to) == send_to.end()) {
    send_to.push_back(m.to);
  }
  // Partner notifications (4.5).
  notify_partner(m.from, peer_event_t{peer_event_kind_t::connected, m.to});
  notify_partner(m.to, peer_event_t{peer_event_kind_t::connected, m.from});
  ack(m.reply_to, m.hdr, true);
}

void router_actor_t::handle_disconnect(disconnect_t &&m) {
  auto from = peers_.find(m.from);
  auto to = peers_.find(m.to);
  if (from == peers_.end() || to == peers_.end()) {
    ack(m.reply_to, m.hdr, false, "disconnect: unknown peer");
    return;
  }
  auto &send_to = from->second.send_to;
  std::erase(send_to, m.to);
  notify_partner(m.from, peer_event_t{peer_event_kind_t::disconnected, m.to});
  notify_partner(m.to, peer_event_t{peer_event_kind_t::disconnected, m.from});
  ack(m.reply_to, m.hdr, true);
}

void router_actor_t::handle_status_req(status_req_t &&m) {
  // Requester-driven gather (D7): answer with the head and scatter to
  // every peer with the requester's reply_to; the router keeps no state.
  status_head_t head;
  head.hdr = m.hdr;
  for (auto &[id, rec] : peers_) {
    head.peers.push_back(peer_meta_t{id, rec.type, rec.send_to, rec.stats});
  }
  head.router_data_drops = mailbox()->data_drops();
  head.router_control_drops = mailbox()->control_drops();
  m.reply_to.post_control(std::move(head));
  for (auto &[id, rec] : peers_) {
    rec.mailbox.post_control(peer_status_req_t{m.hdr, m.reply_to, id});
  }
}

void router_actor_t::handle_peer_command(peer_command_t &&m) {
  auto it = peers_.find(m.peer_id);
  if (it == peers_.end()) {
    m.reply_to.post_control(
        peer_command_resp_t{m.hdr, m.peer_id, "{\"error\":\"Unknown peer\"}",
                            true});
    return;
  }
  // Relay only: the peer answers the requester directly (D7).
  it->second.mailbox.post_control(peer_command_t{
      m.hdr, m.reply_to, m.peer_id, std::move(m.cmd), std::move(m.params_json)});
}

void router_actor_t::handle_subscribe_events(subscribe_events_t &&m) {
  if (!m.reply_to) {
    return;
  }
  subscribers_.push_back(m.reply_to);
}

void router_actor_t::handle_unsubscribe_events(unsubscribe_events_t &&m) {
  std::erase(subscribers_, m.reply_to);
}

void router_actor_t::handle_stop_all(stop_all_t &&m) {
  // Bounded stop-all of spawned peers; hosted ids are stopped by their
  // own supervisor. Acks when every pending remove in the group is done.
  for (auto &[id, rec] : peers_) {
    if (!rec.thread.has_value()) {
      continue;
    }
    pending_removes_[id] =
        pending_remove_t{m.hdr, m.reply_to,
                         std::chrono::steady_clock::now() + remove_deadline_,
                         false};
    rec.mailbox.post_control(stop_t{m.hdr});
  }
  if (pending_removes_.empty()) {
    ack(m.reply_to, m.hdr, true);
  }
}

// ---------------------------------------------------------------------------
// Peer exit handling
// ---------------------------------------------------------------------------

void router_actor_t::handle_stopped(stopped_t &&m) {
  auto pit = pending_removes_.find(m.peer_id);
  if (pit == pending_removes_.end()) {
    // Self-termination (e.g. remote closed the session): same cleanup
    // path as a remove, without an ack (D7/D9).
    INFO("Router: peer id={} self-terminated; implicit remove.", m.peer_id);
    erase_peer(m.peer_id, peer_event_kind_t::stopped);
    return;
  }
  complete_pending_remove(m.peer_id, true, "");
}

void router_actor_t::handle_actor_died(actor_died_t &&m) {
  auto pit = pending_removes_.find(m.id);
  if (pit == pending_removes_.end()) {
    // Implicit remove (4.8): no stop phase, the thread has exited.
    WARNING("Router: peer id={} died reason={}", m.id, quoted_t{m.reason});
    erase_peer(m.id, peer_event_kind_t::died);
    return;
  }
  complete_pending_remove(m.id, false, "peer died: " + m.reason);
}

void router_actor_t::complete_pending_remove(peer_id_t id, bool ok,
                                             const std::string &error) {
  auto pit = pending_removes_.find(id);
  if (pit == pending_removes_.end()) {
    return;
  }
  auto entry = std::move(pit->second);
  pending_removes_.erase(pit);

  auto rec = peers_.find(id);
  if (rec != peers_.end() && rec->second.thread.has_value()) {
    // R1: `stopped`/`actor_died` implies the loop exited, so the join is
    // immediate; the router never blocks otherwise.
    if (rec->second.thread->joinable()) {
      rec->second.thread->join();
    }
  }
  erase_peer(id, ok ? peer_event_kind_t::stopped : peer_event_kind_t::died);

  // Ack when the whole corr group finished (one ack for remove_peer, one
  // ack for stop_all).
  const auto same_group = [&](const auto &kv) {
    return kv.second.hdr.corr == entry.hdr.corr;
  };
  if (std::none_of(pending_removes_.begin(), pending_removes_.end(),
                   same_group)) {
    ack(entry.reply_to, entry.hdr, ok, error);
  }
}

void router_actor_t::check_pending_removes() {
  const auto now = std::chrono::steady_clock::now();
  for (auto it = pending_removes_.begin(); it != pending_removes_.end();) {
    auto &pr = it->second;
    if (now < pr.deadline) {
      ++it;
      continue;
    }
    auto rec = peers_.find(it->first);
    if (!pr.escalated) {
      // Escalation (D9): raise the stop token, grant a short window.
      pr.escalated = true;
      pr.deadline = now + escalation_grace_;
      if (rec != peers_.end() && rec->second.thread.has_value()) {
        WARNING("Router: peer id={} missed its stop deadline; raising stop "
                "token.",
                it->first);
        rec->second.thread->request_stop();
      }
      ++it;
      continue;
    }
    // Still unresolved: delegate the jthread to the supervisor's reaper
    // (the router never blocks), reply with a warning, forget the peer.
    const auto id = it->first;
    auto entry = std::move(pr);
    it = pending_removes_.erase(it);
    std::string reason = "peer did not stop within deadline";
    if (rec != peers_.end() && rec->second.thread.has_value()) {
      if (config_.supervisor_mailbox) {
        config_.supervisor_mailbox.post_control(
            reap_actor_t{std::move(*rec->second.thread), reason});
      }
      rec->second.thread.reset();
    }
    erase_peer(id, peer_event_kind_t::removed);
    ack(entry.reply_to, entry.hdr, false, "removed with warning: " + reason);
  }
}

// ---------------------------------------------------------------------------
// Graph helpers
// ---------------------------------------------------------------------------

void router_actor_t::cut_topology(peer_id_t id) {
  // Empty the removed peer's own send_to and drop it from every partner's
  // send_to, notifying the partners.
  auto removed = peers_.find(id);
  if (removed != peers_.end()) {
    removed->second.send_to.clear();
  }
  for (auto &[pid, rec] : peers_) {
    if (pid == id) {
      continue;
    }
    auto &st = rec.send_to;
    if (std::find(st.begin(), st.end(), id) != st.end()) {
      std::erase(st, id);
      notify_partner(pid, peer_event_t{peer_event_kind_t::disconnected, id});
    }
  }
}

void router_actor_t::erase_peer(peer_id_t id, peer_event_kind_t kind) {
  if (peers_.erase(id) == 0) {
    return;
  }
  INFO("Router: peer id={} removed.", id);
  notify_subscribers(peer_event_t{kind, id});
}

void router_actor_t::notify_partner(peer_id_t to, const peer_event_t &ev) {
  auto it = peers_.find(to);
  if (it != peers_.end() && it->second.mailbox) {
    it->second.mailbox.post_control(ev);
  }
}

void router_actor_t::notify_subscribers(const peer_event_t &ev) {
  for (auto &sub : subscribers_) {
    if (sub) {
      sub.post_control(ev);
    }
  }
}

void router_actor_t::ack(mailbox_handle_t reply_to, const hdr_t &hdr, bool ok,
                         const std::string &error) {
  if (reply_to) {
    reply_to.post_control(ack_t{hdr, ok, error});
  }
}

} // namespace rtpmididns

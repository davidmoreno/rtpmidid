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

#include "alsa_actor.hpp"
#include "settings.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/logger.hpp"
#include <alsa/asoundlib.h>
#include <charconv>

namespace rtpmididns {

alsa_actor_t::alsa_actor_t(actor_config_t config, std::string alsa_name,
                           std::vector<std::string> announce_names,
                           std::shared_ptr<router_mailbox_t> router_mailbox,
                           std::shared_ptr<server_mailbox_t> server_mailbox)
    : actor_t(std::move(config)), alsa_name_(std::move(alsa_name)),
      announce_names_(std::move(announce_names)),
      router_mailbox_(std::move(router_mailbox)),
      server_mailbox_(std::move(server_mailbox)) {}

void alsa_actor_t::on_start() {
  try {
    // The seq fd is registered in THIS actor's poller.
    seq_ = std::make_unique<aseq_t>(alsa_name_, poller());
    INFO("ALSA listener {}: sequencer client {} ready.", name(),
         seq_->client_id);
  } catch (const std::exception &e) {
    ERROR("ALSA listener {}: sequencer open failed: {}", name(), e.what());
    seq_ = nullptr;
    return;
  }
  for (auto &port_name : announce_names_) {
    create_port(port_name);
  }
  // ALSA hardware auto-export (design D6): enumerate the local ports and
  // track add/remove through the System Announce port.
  if (settings.alsa_hw_auto_export.type !=
      settings_t::alsa_hw_auto_export_type_e::NONE) {
    try {
      announce_port_ = seq_->create_port("Announcements", false);
      announce_connections_.push_back(
          seq_->connect(aseq_t::port_t{0, 1},
                        aseq_t::port_t{seq_->client_id, announce_port_}));
      added_announcement_conn_ = seq_->added_port_announcement.connect(
          [this](const std::string &n, aseq_t::client_type_e t,
                 const aseq_t::port_t &p) { auto_export_add(n, t, p); });
      removed_announcement_conn_ = seq_->removed_port_announcement.connect(
          [this](const aseq_t::port_t &p) { auto_export_remove(p); });
      auto_export_enumerate();
    } catch (const std::exception &e) {
      WARNING("ALSA listener {}: auto-export announce subscription failed: {}",
              name(), e.what());
    }
  }
}

void alsa_actor_t::on_stop() {
  // Hosted ids are unregistered by the router's implicit remove when this
  // actor posts stopped; the seq closes with the actor.
  added_announcement_conn_.disconnect();
  removed_announcement_conn_.disconnect();
  announce_connections_.clear();
  ports_.clear();
  seq_.reset();
}

uint8_t alsa_actor_t::create_port(const std::string &port_name, bool waiting,
                                  const std::string &remote) {
  if (!seq_) {
    return 0;
  }
  auto &p = make_port(port_name,
                      waiting ? port_kind_t::waiting
                              : port_kind_t::announced,
                      "", remote);
  if (!waiting) {
    begin_register(p);
  } else {
    INFO("ALSA listener {}: waiting port '{}' (seq {}) for remote '{}'.",
         name(), port_name, p.seq_port, remote);
  }
  return p.seq_port;
}

alsa_actor_t::port_state_t &
alsa_actor_t::make_port(const std::string &port_name, port_kind_t kind,
                        const std::string &meta, const std::string &remote) {
  const uint8_t seq_port = seq_->create_port(port_name);
  auto &p = ports_[seq_port];
  p.seq_port = seq_port;
  p.name = port_name;
  p.meta = meta;
  p.remote = remote;
  p.kind = kind;

  // Incoming seq events on this port -> midi_received{from=assigned id}.
  p.conns.midi =
      seq_->midi_event[seq_port].connect([this, seq_port](snd_seq_event_t *ev) {
        auto pit = ports_.find(seq_port);
        if (pit == ports_.end() || pit->second.router_id == 0) {
          return; // not registered yet (ack pending)
        }
        rtpmidid::io_bytes_static<1024> data;
        auto writer = rtpmidid::io_bytes_writer(data);
        mididata_decoder_.ev_to_mididata_f(
            ev, writer,
            [this, id = pit->second.router_id](const mididata_t &md) {
              auto payload = midi_payload_t::make(md.start, md.size());
              if (payload) {
                router_mailbox_->post_data(
                    data_message_t::midi_received(id, std::move(*payload)));
              }
            });
      });

  // Subscription tracking: waiting ports register on the first ALSA
  // subscription and ask the rtpmidi server for the session; the last
  // unsubscribe tears it down (design D2/D8).
  p.conns.subscribe = seq_->subscribe_event[seq_port].connect(
      [this, seq_port](aseq_t::port_t other, const std::string &) {
        auto pit = ports_.find(seq_port);
        if (pit != ports_.end()) {
          on_subscribe(pit->second, other);
        }
      });
  p.conns.unsubscribe = seq_->unsubscribe_event[seq_port].connect(
      [this, seq_port](aseq_t::port_t other) {
        auto pit = ports_.find(seq_port);
        if (pit != ports_.end()) {
          on_unsubscribe(pit->second, other);
        }
      });
  return p;
}

void alsa_actor_t::on_subscribe(port_state_t &p, const aseq_t::port_t &other) {
  // The daemon's own wiring (auto-export subscription ports) does not
  // count as an external ALSA subscriber.
  if (p.kind == port_kind_t::subscription && seq_ &&
      other.client == seq_->client_id) {
    return;
  }
  p.subscribers++;
  INFO("ALSA listener: port {} ('{}') subscribed ({} subscriber(s)).",
       p.seq_port, p.name, p.subscribers);
  if (p.kind != port_kind_t::waiting || p.subscribers != 1) {
    return;
  }
  // First subscription to a waiting port: register it as a hosted peer and
  // request the session from the rtpmidi server.
  if (p.registered && p.router_id != 0) {
    post_session_request(p, true);
  } else {
    if (!p.register_pending) {
      begin_register(p);
    }
    p.pending_session_requests.push_back(true);
  }
}

void alsa_actor_t::on_unsubscribe(port_state_t &p,
                                  const aseq_t::port_t &other) {
  if (p.kind == port_kind_t::subscription && seq_ &&
      other.client == seq_->client_id) {
    return;
  }
  if (p.subscribers > 0) {
    p.subscribers--;
  }
  INFO("ALSA listener: port {} ('{}') unsubscribed ({} subscriber(s)).",
       p.seq_port, p.name, p.subscribers);
  if (p.kind != port_kind_t::waiting || p.subscribers != 0) {
    return;
  }
  // Last unsubscribe: report it to the server, and unregister the port
  // unless a live session is wired to it (it stays registered for the
  // lifetime of the session).
  if (p.router_id != 0) {
    post_session_request(p, false);
  } else {
    p.pending_session_requests.push_back(false);
  }
  if (!p.has_session && p.registered && !p.register_pending) {
    unregister_from_router(p);
  }
}

void alsa_actor_t::post_session_request(port_state_t &p, bool subscribed) {
  if (!server_mailbox_) {
    WARNING("ALSA listener: no rtpmidi server wired; session request for "
            "'{}' dropped.",
            p.remote);
    return;
  }
  server_mailbox_->post_control(session_request_t{
      hdr_t{0}, {}, p.seq_port, p.router_id, p.remote, subscribed});
}

void alsa_actor_t::begin_register(port_state_t &p) {
  if (!router_mailbox_ || p.register_pending) {
    return;
  }
  p.register_pending = true;
  auto reply = std::make_shared<reply_mailbox_t>();
  pending_registers_[p.seq_port] = reply;
  router_mailbox_->post_control(register_peer_t{
      hdr_t{uint64_t(p.seq_port) + 1}, reply, mailbox_handle(), "alsa",
      p.name});
}

void alsa_actor_t::finish_register(port_state_t &p, peer_id_t id) {
  p.register_pending = false;
  p.registered = true;
  p.router_id = id;
  id_to_port_[id] = p.seq_port;
  INFO("ALSA listener: port {} ('{}') registered as peer id {}.", p.seq_port,
       p.name, id);
  for (auto &[mb, hdr] : p.create_replies) {
    if (mb) {
      mb.post_control(alsa_port_result_t{hdr, p.seq_port, id});
    }
  }
  p.create_replies.clear();
  for (auto &[mb, hdr] : p.set_registered_replies) {
    if (mb) {
      mb.post_control(alsa_port_result_t{hdr, p.seq_port, id});
    }
  }
  p.set_registered_replies.clear();
  for (const bool subscribed : p.pending_session_requests) {
    post_session_request(p, subscribed);
  }
  p.pending_session_requests.clear();
  // The registration raced with the last unsubscribe / a session end.
  if (p.kind == port_kind_t::waiting && p.subscribers == 0 && !p.has_session) {
    unregister_from_router(p);
  }
}

void alsa_actor_t::register_failed(port_state_t &p) {
  p.register_pending = false;
  for (auto &[mb, hdr] : p.create_replies) {
    if (mb) {
      mb.post_control(alsa_port_result_t{hdr, p.seq_port, 0});
    }
  }
  p.create_replies.clear();
  for (auto &[mb, hdr] : p.set_registered_replies) {
    if (mb) {
      mb.post_control(alsa_port_result_t{hdr, p.seq_port, 0});
    }
  }
  p.set_registered_replies.clear();
  p.pending_session_requests.clear();
}

void alsa_actor_t::unregister_from_router(port_state_t &p) {
  if (!p.registered) {
    return;
  }
  if (router_mailbox_ && p.router_id != 0) {
    router_mailbox_->post_control(unregister_peer_t{
        hdr_t{uint64_t(p.seq_port) + 1}, mailbox(), p.router_id});
    INFO("ALSA listener: port {} ('{}') unregistered (peer id {}).",
         p.seq_port, p.name, p.router_id);
  }
  id_to_port_.erase(p.router_id);
  p.router_id = 0;
  p.registered = false;
}

void alsa_actor_t::remove_port(uint8_t seq_port) {
  auto it = ports_.find(seq_port);
  if (it == ports_.end()) {
    return;
  }
  auto &p = it->second;
  INFO("ALSA listener: removing port {} ('{}').", seq_port, p.name);
  unregister_from_router(p);
  if (seq_) {
    seq_->remove_port(seq_port);
  }
  ports_.erase(it);
}

void alsa_actor_t::on_control(alsa_control_t &&msg) {
  std::visit(
      [this](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, alsa_create_port_t>) {
          if (m.name.empty() || !seq_) {
            if (m.reply_to) {
              m.reply_to.post_control(alsa_port_result_t{m.hdr, 0, 0});
            }
            return;
          }
          auto &p = make_port(m.name,
                              m.waiting ? port_kind_t::waiting
                                        : port_kind_t::per_connection,
                              m.meta, m.remote);
          if (m.waiting) {
            INFO("ALSA listener: waiting port '{}' (seq {}) for remote '{}'.",
                 m.name, p.seq_port, m.remote);
            if (m.reply_to) {
              m.reply_to.post_control(
                  alsa_port_result_t{m.hdr, p.seq_port, 0});
            }
            return;
          }
          // Registered port: the create request completes when the register
          // ack arrives; echo the original correlation id.
          if (m.reply_to) {
            p.create_replies.emplace_back(m.reply_to, m.hdr);
          }
          begin_register(p);
        } else if constexpr (std::is_same_v<T, alsa_remove_port_t>) {
          uint8_t seq_port = m.seq_port;
          if (seq_port == 0 && m.peer_id != 0) {
            auto it = id_to_port_.find(m.peer_id);
            if (it != id_to_port_.end()) {
              seq_port = it->second;
            }
          }
          if (seq_port != 0) {
            remove_port(seq_port);
          }
          if (m.reply_to) {
            m.reply_to.post_control(ack_t{m.hdr, true, {}});
          }
        } else if constexpr (std::is_same_v<T, alsa_subscribe_port_t>) {
          // Auto-export connection: a per-connection port subscribed both
          // ways to the exported local port.
          if (!seq_ || m.target.empty()) {
            if (m.reply_to) {
              m.reply_to.post_control(alsa_port_result_t{m.hdr, 0, 0});
            }
            return;
          }
          uint8_t client = 0, lport = 0;
          {
            const auto colon = m.target.find(':');
            if (colon == std::string::npos ||
                std::from_chars(m.target.data(), m.target.data() + colon,
                                client)
                        .ec != std::errc{} ||
                std::from_chars(m.target.data() + colon + 1,
                                m.target.data() + m.target.size(), lport)
                        .ec != std::errc{}) {
              ERROR("ALSA listener: bad subscribe target '{}'.", m.target);
              if (m.reply_to) {
                m.reply_to.post_control(alsa_port_result_t{m.hdr, 0, 0});
              }
              return;
            }
          }
          auto &p = make_port(m.name, port_kind_t::subscription, m.target, "");
          try {
            p.local_conns.push_back(
                seq_->connect(aseq_t::port_t{client, lport},
                              aseq_t::port_t{seq_->client_id, p.seq_port}));
            p.local_conns.push_back(
                seq_->connect(aseq_t::port_t{seq_->client_id, p.seq_port},
                              aseq_t::port_t{client, lport}));
          } catch (const std::exception &e) {
            ERROR("ALSA listener: cannot subscribe to {}: {}.", m.target,
                  e.what());
            remove_port(p.seq_port);
            if (m.reply_to) {
              m.reply_to.post_control(alsa_port_result_t{m.hdr, 0, 0});
            }
            return;
          }
          if (m.reply_to) {
            p.create_replies.emplace_back(m.reply_to, m.hdr);
          }
          begin_register(p);
        } else if constexpr (std::is_same_v<T, alsa_port_session_t>) {
          auto pit = ports_.find(m.seq_port);
          if (pit == ports_.end()) {
            return;
          }
          auto &p = pit->second;
          p.has_session = m.has_session;
          // Session end with nobody subscribed: the port goes back to
          // waiting (unregistered).
          if (!m.has_session && p.subscribers == 0 && p.registered &&
              !p.register_pending) {
            unregister_from_router(p);
          }
        } else if constexpr (std::is_same_v<T, alsa_port_set_registered_t>) {
          auto pit = ports_.find(m.seq_port);
          if (pit == ports_.end()) {
            if (m.reply_to) {
              m.reply_to.post_control(alsa_port_result_t{m.hdr, m.seq_port, 0});
            }
            return;
          }
          auto &p = pit->second;
          if (m.registered) {
            if (p.registered && p.router_id != 0) {
              if (m.reply_to) {
                m.reply_to.post_control(
                    alsa_port_result_t{m.hdr, p.seq_port, p.router_id});
              }
            } else {
              if (m.reply_to) {
                p.set_registered_replies.emplace_back(m.reply_to, m.hdr);
              }
              begin_register(p);
            }
          } else {
            // Unregister only when nobody is subscribed; active subscribers
            // keep the port registered.
            if (p.registered && !p.register_pending && p.subscribers == 0) {
              unregister_from_router(p);
            }
            if (m.reply_to) {
              m.reply_to.post_control(
                  alsa_port_result_t{m.hdr, p.seq_port, p.router_id});
            }
          }
        } else if constexpr (std::is_same_v<T, exports_status_req_t>) {
          handle_exports_status(std::move(m));
        }
      },
      msg);
}

void alsa_actor_t::handle_exports_status(exports_status_req_t &&m) {
  if (!m.reply_to) {
    return;
  }
  exports_status_resp_t resp;
  resp.hdr = m.hdr;
  resp.source = "alsa";
  for (auto &[seq_port, p] : ports_) {
    if (p.kind != port_kind_t::waiting) {
      continue;
    }
    export_status_entry_t e;
    e.name = p.name;
    e.kind = "waiting";
    e.target = p.meta;
    e.state = p.has_session          ? "connected"
              : p.subscribers > 0    ? "subscribed"
                                       : "waiting";
    resp.exports.push_back(std::move(e));
  }
  m.reply_to.post_control(std::move(resp));
}

void alsa_actor_t::on_data(data_message_t &&msg) {
  if (msg.kind != data_message_t::kind_t::midi_to_wire) {
    return;
  }
  auto it = id_to_port_.find(msg.to);
  if (it == id_to_port_.end() || !seq_) {
    WARNING_RATE_LIMIT(5, "ALSA listener {}: midi_to_wire to unknown id {}.",
                       name(), msg.to);
    return;
  }
  const uint8_t port = it->second;
  rtpmidid::io_bytes_reader reader(msg.payload.data(),
                                   uint32_t(msg.payload.size()));
  mididata_encoder_.mididata_to_evs_f(reader, [this, port](snd_seq_event_t *ev) {
    snd_seq_ev_set_source(ev, port);
    snd_seq_ev_set_subs(ev); // to all subscribers
    snd_seq_ev_set_direct(ev);
    const int r = snd_seq_event_output(seq_->seq, ev);
    if (r == -EAGAIN) {
      // EAGAIN: seq output buffer full; retry when POLLOUT is available.
      output_pending_ = true;
    } else if (r < 0) {
      ERROR("ALSA listener {}: event output: {}", name(), snd_strerror(r));
      snd_seq_drop_output(seq_->seq);
    }
  });
  flush_output();
}

void alsa_actor_t::flush_output() {
  if (!seq_) {
    return;
  }
  const int r = snd_seq_drain_output(seq_->seq);
  if (r == -EAGAIN) {
    output_pending_ = true;
  } else {
    output_pending_ = false;
    if (r < 0) {
      ERROR("ALSA listener {}: drain output: {}", name(), snd_strerror(r));
      snd_seq_drop_output(seq_->seq);
    }
  }
}

void alsa_actor_t::on_loop() {
  // Collect register_peer acks from the per-port reply mailboxes.
  for (auto it = pending_registers_.begin(); it != pending_registers_.end();) {
    bool done = false;
    while (auto c = it->second->pop_control()) {
      if (auto *r = std::get_if<peer_ids_result_t>(&*c)) {
        auto pit = ports_.find(it->first);
        if (!r->ids.empty()) {
          if (pit != ports_.end()) {
            finish_register(pit->second, r->ids[0]);
          } else if (router_mailbox_) {
            // The port was removed while the registration was in flight:
            // drop the late id again.
            router_mailbox_->post_control(
                unregister_peer_t{hdr_t{0}, mailbox(), r->ids[0]});
          }
        } else if (pit != ports_.end()) {
          register_failed(pit->second);
        }
        done = true;
        break;
      }
    }
    if (done) {
      it = pending_registers_.erase(it);
    } else {
      ++it;
    }
  }
  // EAGAIN/POLLOUT retry for seq output.
  if (output_pending_) {
    flush_output();
  }
}

// --- introspection -----------------------------------------------------------

std::vector<uint8_t> alsa_actor_t::announced_ports() const {
  std::vector<uint8_t> v;
  for (auto &[seq_port, p] : ports_) {
    if (p.kind == port_kind_t::announced) {
      v.push_back(seq_port);
    }
  }
  return v;
}

bool alsa_actor_t::port_registered(uint8_t seq_port) const {
  auto it = ports_.find(seq_port);
  return it != ports_.end() && it->second.router_id != 0;
}

int alsa_actor_t::port_subscribers(uint8_t seq_port) const {
  auto it = ports_.find(seq_port);
  return it == ports_.end() ? 0 : it->second.subscribers;
}

alsa_actor_t::port_kind_t alsa_actor_t::port_kind(uint8_t seq_port) const {
  auto it = ports_.find(seq_port);
  return it == ports_.end() ? port_kind_t::announced : it->second.kind;
}

bool alsa_actor_t::waiting_port_for(const std::string &remote,
                                    uint8_t &seq_port) const {
  for (auto &[sp, p] : ports_) {
    if (p.kind == port_kind_t::waiting && p.remote == remote) {
      seq_port = sp;
      return true;
    }
  }
  return false;
}

// --- ALSA hardware auto-export (design D6) ------------------------------------

bool alsa_actor_t::matches_auto_export(const std::string &name,
                                       aseq_t::client_type_e type) const {
  const auto &s = settings.alsa_hw_auto_export;
  if (s.type == settings_t::alsa_hw_auto_export_type_e::NONE) {
    return false;
  }
  if (s.type != settings_t::alsa_hw_auto_export_type_e::ALL) {
    const int bit = type == aseq_t::client_type_e::TYPE_HARDWARE   ? 1
                    : type == aseq_t::client_type_e::TYPE_SOFTWARE ? 2
                                                                     : 4;
    if ((int(s.type) & bit) == 0) {
      return false;
    }
  }
  if (s.name_positive_regex &&
      !std::regex_match(name, *s.name_positive_regex)) {
    return false;
  }
  if (s.name_negative_regex &&
      std::regex_match(name, *s.name_negative_regex)) {
    return false;
  }
  return true;
}

void alsa_actor_t::auto_export_enumerate() {
  if (!seq_) {
    return;
  }
  seq_->for_devices([this](uint8_t device_id, const std::string &device_name,
                           aseq_t::client_type_e type) {
    if (device_id == seq_->client_id) {
      return; // never export the daemon's own ports
    }
    seq_->for_ports(
        device_id, [&](uint8_t port_id, const std::string &) {
          auto_export_add(device_name, type,
                          aseq_t::port_t{device_id, port_id});
        });
  });
}

std::string alsa_actor_t::get_port_name(const aseq_t::port_t &port) const {
  snd_seq_port_info_t *info = nullptr;
  snd_seq_port_info_alloca(&info);
  if (snd_seq_get_any_port_info(seq_->seq, port.client, port.port, info) < 0) {
    return std::to_string(port.port);
  }
  return snd_seq_port_info_get_name(info);
}

void alsa_actor_t::auto_export_add(const std::string &client_name,
                                   aseq_t::client_type_e type,
                                   const aseq_t::port_t &port) {
  if (!seq_ || !server_mailbox_) {
    return;
  }
  if (port.client == seq_->client_id) {
    return; // never export the daemon's own ports (no self-loop)
  }
  if (!matches_auto_export(client_name, type)) {
    return;
  }
  const uint16_t key = uint16_t((port.client << 8) + port.port);
  if (auto_exports_.count(key) != 0) {
    return;
  }
  const auto export_name =
      FMT::format("{} {}", client_name, get_port_name(port));
  auto_exports_[key] = export_name;
  INFO("ALSA listener: auto-exporting seq port {}:{} as '{}'.", port.client,
       port.port, export_name);
  server_mailbox_->post_control(export_add_t{
      hdr_t{0}, {}, export_name, export_kind_e::seq,
      FMT::format("{}:{}", port.client, port.port), 0});
}

void alsa_actor_t::auto_export_remove(const aseq_t::port_t &port) {
  const uint16_t key = uint16_t((port.client << 8) + port.port);
  auto it = auto_exports_.find(key);
  if (it == auto_exports_.end()) {
    return;
  }
  INFO("ALSA listener: auto-export for seq port {}:{} removed.", port.client,
       port.port);
  if (server_mailbox_) {
    server_mailbox_->post_control(
        export_remove_t{hdr_t{0}, {}, it->second});
  }
  auto_exports_.erase(it);
}

} // namespace rtpmididns

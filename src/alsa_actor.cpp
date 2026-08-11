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

#include "alsa_actor.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/logger.hpp"
#include <alsa/asoundlib.h>

namespace rtpmididns {

alsa_actor_t::alsa_actor_t(actor_config_t config, std::string alsa_name,
                           std::vector<std::string> announce_names,
                           mailbox_handle_t router_mailbox)
    : actor_t(std::move(config)), alsa_name_(std::move(alsa_name)),
      announce_names_(std::move(announce_names)),
      router_mailbox_(std::move(router_mailbox)) {}

void alsa_actor_t::on_start() {
  try {
    // The seq fd is registered in THIS actor's poller.
    seq_ = std::make_unique<aseq_t>(alsa_name_, poller());
    INFO("ALSA actor {}: sequencer client {} ready.", name(), seq_->client_id);
  } catch (const std::exception &e) {
    ERROR("ALSA actor {}: sequencer open failed: {}", name(), e.what());
    seq_ = nullptr;
    return;
  }
  for (auto &port_name : announce_names_) {
    create_port(port_name);
  }
}

void alsa_actor_t::on_stop() {
  // Hosted ids are unregistered by the router's implicit remove when this
  // actor posts stopped; the seq closes with the actor.
  seq_.reset();
}

uint8_t alsa_actor_t::create_port(const std::string &port_name) {
  if (!seq_) {
    return 0;
  }
  const uint8_t seq_port = seq_->create_port(port_name);
  register_port(seq_port, port_name);
  return seq_port;
}

void alsa_actor_t::register_port(uint8_t seq_port, const std::string &name) {
  if (!router_mailbox_) {
    return;
  }
  auto reply = std::make_shared<actor_mailbox_t>();
  router_mailbox_->post_control(register_peer_t{
      hdr_t{uint64_t(seq_port) + 1}, reply, mailbox(), "alsa", name});
  pending_ports_[seq_port] = reply;

  // Incoming seq events on this port -> midi_received{from=assigned id}.
  port_connections_[seq_port].midi =
      seq_->midi_event[seq_port].connect([this, seq_port](snd_seq_event_t *ev) {
        auto it = port_to_id_.find(seq_port);
        if (it == port_to_id_.end()) {
          return; // not registered yet (ack pending)
        }
        rtpmidid::io_bytes_static<1024> data;
        auto writer = rtpmidid::io_bytes_writer(data);
        mididata_decoder_.ev_to_mididata_f(
            ev, writer, [this, id = it->second](const mididata_t &md) {
              auto payload = midi_payload_t::make(md.start, md.size());
              if (payload) {
                router_mailbox_->post_data(
                    data_message_t::midi_received(id, std::move(*payload)));
              }
            });
      });
}

void alsa_actor_t::unregister_port(uint8_t seq_port) {
  auto it = port_to_id_.find(seq_port);
  if (it == port_to_id_.end()) {
    return;
  }
  if (router_mailbox_) {
    router_mailbox_->post_control(
        unregister_peer_t{hdr_t{uint64_t(seq_port) + 1}, mailbox(), it->second});
  }
  id_to_port_.erase(it->second);
  port_to_id_.erase(it);
  seq_->remove_port(seq_port);
}

void alsa_actor_t::on_control(control_message_t &&) {
  // Everything the ALSA actor needs arrives on the data lane or is
  // collected by on_loop (register acks on per-port reply mailboxes).
}

void alsa_actor_t::on_data(data_message_t &&msg) {
  if (msg.kind != data_message_t::kind_t::midi_to_wire) {
    return;
  }
  auto it = id_to_port_.find(msg.to);
  if (it == id_to_port_.end() || !seq_) {
    WARNING_RATE_LIMIT(5, "ALSA actor {}: midi_to_wire to unknown id {}.",
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
      ERROR("ALSA actor {}: event output: {}", name(), snd_strerror(r));
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
      ERROR("ALSA actor {}: drain output: {}", name(), snd_strerror(r));
      snd_seq_drop_output(seq_->seq);
    }
  }
}

void alsa_actor_t::on_loop() {
  // Collect register_peer acks from the per-port reply mailboxes.
  for (auto it = pending_ports_.begin(); it != pending_ports_.end();) {
    bool done = false;
    while (auto c = it->second->pop_control()) {
      if (auto *r = std::get_if<peer_ids_result_t>(&c->v)) {
        if (!r->ids.empty()) {
          id_to_port_[r->ids[0]] = it->first;
          port_to_id_[it->first] = r->ids[0];
        }
        done = true;
        break;
      }
    }
    if (done) {
      it = pending_ports_.erase(it);
    } else {
      ++it;
    }
  }
  // EAGAIN/POLLOUT retry for seq output.
  if (output_pending_) {
    flush_output();
  }
}

} // namespace rtpmididns

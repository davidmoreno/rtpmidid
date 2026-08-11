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

#include "local_rawmidi_peer_actor.hpp"
#include "peer_status_jsondm.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/packet.hpp"
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace rtpmididns {

local_rawmidi_peer_actor_t::local_rawmidi_peer_actor_t(actor_config_t config,
                                                       std::string device,
                                                       std::string name,
                                                       int fd)
    : peer_actor_t(std::move(config)), device_(std::move(device)),
      name_(std::move(name)), fd_(fd) {}

void local_rawmidi_peer_actor_t::on_start() {
  peer_actor_t::on_start(); // the `registered` gate
  if (fd_ < 0) {
    WARNING("Rawmidi peer {}: no fd; read disabled.", name());
    return;
  }
  fd_listener_ = add_fd_in(fd_, [this](int) { read_midi(); });
  INFO("Rawmidi actor {}: reading device {}", name(), device_);
}

void local_rawmidi_peer_actor_t::on_stop() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void local_rawmidi_peer_actor_t::read_midi() {
  if (fd_ < 0) {
    return;
  }
  const ssize_t count = ::read(fd_, buffer_.data(), buffer_.size());
  if (count <= 0) {
    return;
  }
  rtpmidid::packet_t packet(buffer_.begin(), static_cast<uint32_t>(count));
  normalizer_.normalize_stream(packet, [this](const rtpmidid::packet_t &p) {
    auto payload = midi_payload_t::make(p.get_data(), p.get_size());
    if (payload) {
      // Oversized payloads (sysex floods) go through the heap escape pool.
      send_to_router(config_.id, std::move(*payload));
    }
  });
}

void local_rawmidi_peer_actor_t::send_to_wire(peer_id_t to, peer_id_t from,
                                              midi_payload_t &&payload) {
  (void)to;
  (void)from;
  if (fd_ < 0) {
    return;
  }
  const ssize_t n = ::write(fd_, payload.data(), payload.size());
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      WARNING_RATE_LIMIT(5, "Rawmidi peer {}: device buffer full; message "
                            "dropped.",
                         name_);
    } else {
      ERROR("Rawmidi peer {}: write error: {}", name_, strerror(errno));
    }
  }
}

peer_status_variant_t local_rawmidi_peer_actor_t::status() {
  rawmidi_peer_status_t s;
  s.name = name_;
  s.device = device_;
  s.status = fd_ >= 0 ? "open" : "closed";
  return s;
}

} // namespace rtpmididns

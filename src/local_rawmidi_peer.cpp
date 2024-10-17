/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include <fcntl.h>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/packet.hpp>
#include <rtpmidid/poller.hpp>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "json.hpp"
#include "local_rawmidi_peer.hpp"
#include "mididata.hpp"
#include "midirouter.hpp"

using namespace rtpmididns;

local_rawmidi_peer_t::local_rawmidi_peer_t(const std::string &name_,
                                           const std::string &device_)
    : device(device_), name(name_) {
  INFO("Creating rawmidi peer=\"{}\", device={}", name, device);
  fd = open(device.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    if (fd == ENOENT) {
      WARNING("Device {} does not exist. Try to create as pipe.", device);
      if (mkfifo(device.c_str(), 0666) == 0) {
        fd = open(device.c_str(), O_RDWR | O_NONBLOCK);
      }
    }
    if (fd < 0) {
      ERROR("Error opening rawmidi {}: {}", device, strerror(errno));
      return;
    }
  }
  try {
    fd_listener =
        rtpmidid::poller.add_fd_in(fd, [this](int fd) { read_midi(); });
  } catch (const std::exception &e) {
    ERROR("Error adding rawmidi {}: {}. Will allow writing, no reading.",
          device, e.what());
  }
}

local_rawmidi_peer_t::~local_rawmidi_peer_t() {
  if (fd >= 0) {
    close(fd);
  }
}

json_t local_rawmidi_peer_t::status() {
  json_t j{
      {"name", name},
      {"device", device},
      {"status", fd >= 0 ? "open" : "closed"}
      //
  };
  return j;
}

void local_rawmidi_peer_t::send_midi(midipeer_id_t from,
                                     const mididata_t &data) {
  if (fd < 0) {
    return;
  }
  int ret = write(fd, data.start, data.size());
  if (ret < 0) {
    ERROR("Error writing to rawmidi {}: {}", device, strerror(errno));
    WARNING("Will not try again.");
  }
}

void local_rawmidi_peer_t::read_midi() {
  if (fd < 0) {
    return;
  }
  std::array<uint8_t, 1024> adata;
  ssize_t count = read(fd, adata.data(), adata.size());

  if (count <= 0) {
    return;
  }
  rtpmidid::packet_t packet(adata.begin(), (uint32_t)count);

  // FIXME: Even if received several message in the stream, send one by one.
  // Maybe would be better send full packets, but would need some stack space or
  // something...
  midi_normalizer.normalize_stream(
      packet, [&](const rtpmidid::packet_t &packet) {
        router->send_midi(peer_id, mididata_t{packet.get_data(),
                                              (uint32_t)packet.get_size()});
      });
}
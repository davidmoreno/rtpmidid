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

#include <alsa/asoundlib.h>
#include <fcntl.h>
#include <fstream>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/packet.hpp>
#include <rtpmidid/poller.hpp>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <glob.h>
#include <unistd.h>

#include "peer_device_rawmidi.hpp"
#include "mididata.hpp"
#include "midirouter.hpp"
#include "stringpp.hpp"
#include <alsa/rawmidi.h>

using namespace rtpmididns;

static std::string get_rawmidi_name(const std::string &device);

peer_device_rawmidi_t::peer_device_rawmidi_t(const std::string &name_,
                                           const std::string &device_)
    : device(device_), name(name_) {
  if (name == "") {
    name = get_rawmidi_name(device);
    INFO("Guessed name device={} name={}", name, device);
  }
}

void peer_device_rawmidi_t::open() {
  assert(fd < 0);
  buffer.fill(0);
  INFO("Creating rawmidi peer=\"{}\", device={}", name, device);
  fd = ::open(device.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    if (fd == ENOENT) {
      WARNING("Device {} does not exist. Try to create as pipe.", device);
      if (mkfifo(device.c_str(), 0666) == 0) {
        fd = ::open(device.c_str(), O_RDWR | O_NONBLOCK);
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

void peer_device_rawmidi_t::close() {
  if (fd >= 0) {
    fd_listener.stop();
    ::close(fd);
    fd = -1;
  }
}

peer_device_rawmidi_t::~peer_device_rawmidi_t() { close(); }

router_peer_row_t peer_device_rawmidi_t::status() const {
  router_peer_row_t row;
  row.name = name;
  row.device = device;
  row.status = fd >= 0 ? "open" : "closed";
  return row;
}

void peer_device_rawmidi_t::send_midi(midipeer_id_t from,
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

void peer_device_rawmidi_t::read_midi() {
  if (fd < 0) {
    return;
  }
  ssize_t count = read(fd, buffer.data(), buffer.size());

  if (count <= 0) {
    return;
  }
  rtpmidid::packet_t packet(buffer.begin(), (uint32_t)count);

  // FIXME: Even if received several message in the stream, send one by one.
  // Maybe would be better send full packets, but would need some stack space
  // or something...
  midi_normalizer.normalize_stream(
      packet, [&](const rtpmidid::packet_t &packet) {
        enqueue_to_router(mididata_t{packet.get_data(),
                                              (uint32_t)packet.get_size()});
      });
}

void peer_device_rawmidi_t::event(midipeer_event_e event,
                                 midipeer_id_t peer_id) {
  DEBUG("event={} from={}", event, peer_id);
  switch (event) {
  case midipeer_event_e::CONNECTED_PEER:
    connected(peer_id);
    break;
  case midipeer_event_e::DISCONNECTED_PEER:
    disconnected(peer_id);
    break;
  default:
    DEBUG("Ignore event={} from={}", event, peer_id);
  }
  // default behaviour, logging
  midipeer_t::event(event, peer_id);
}
void peer_device_rawmidi_t::connected(midipeer_id_t peer_id) {
  connection_count++;
  if (connection_count == 1) {
    INFO("Open rawmidi {}", device);
    open();
  }
  DEBUG("Connected to rawmidi device={} count={}", device, connection_count);
}

void peer_device_rawmidi_t::disconnected(midipeer_id_t peer_id) {
  connection_count--;
  if (connection_count == 0) {
    INFO("Close rawmidi {}", device);
    close();
  }
  DEBUG("Disconnected to rawmidi device={} count={}", device, connection_count);
}

static std::string get_name_from_devname(const std::string &device);
static std::string get_name_from_alsalib(int card_id, int device_id);

static std::string get_rawmidi_name(const std::string &device) {
  std::string name;

  auto devname = rtpmididns::split(device, '/').back();
  if (std::startswith(devname, "midiC")) {
    auto CDpart = devname.substr(4);
    auto Dindex = CDpart.find('D');
    try {
      int card_id = std::stoi(CDpart.substr(1, Dindex - 1));
      int device_id =
          std::stoi(CDpart.substr(Dindex + 1, CDpart.size() - Dindex - 1));

      name = get_name_from_alsalib(card_id, device_id);
    } catch (const std::exception &e) {
      WARNING("Error parsing device={} error={}", device, e.what());
      DEBUG("CDpart={} Dindex={} C={} D={}", CDpart, Dindex,
            CDpart.substr(1, Dindex - 1),
            CDpart.substr(Dindex, CDpart.size() - Dindex));
    }
  }
  if (name == "") {
    name = get_name_from_devname(devname);
  }
  return name;
}

static std::string get_name_from_alsalib(int card_id, int device_id) {
  std::string name;
  char cardname[32];
  snd_ctl_t *ctl;
  snd_rawmidi_info_t *rawmidi_info;

  sprintf(cardname, "hw:%d",
          card_id); // Format the card name as "hw:<card_number>"
  if (snd_ctl_open(&ctl, cardname, 0) < 0) {
    DEBUG("Error opening control interface for card {}", card_id);
    return "";
  }
  snd_rawmidi_info_alloca(&rawmidi_info);
  snd_rawmidi_info_set_device(rawmidi_info, device_id);
  snd_rawmidi_info_set_stream(rawmidi_info, SND_RAWMIDI_STREAM_OUTPUT);
  if (snd_ctl_rawmidi_info(ctl, rawmidi_info) >= 0) {
    const char *cname = snd_rawmidi_info_get_name(rawmidi_info);
    name = cname;
    DEBUG("card={} device={} name={}", card_id, device_id, name);
  }

  snd_ctl_close(ctl);
  return name;
}

static std::string get_name_from_devname(const std::string &device) {
  std::string name;
  auto dev = rtpmididns::split(device, '/').back();

  std::string sysfs = FMT::format("/sys/class/sound/{}/device/id", dev);
  DEBUG("Checking for MIDI device={} in sysfs={}", device, sysfs);
  std::ifstream file(sysfs);
  if (file.is_open()) {
    std::getline(file, name);
  }
  return name;
}

namespace rtpmididns {

std::vector<rawmidi_device_row_t> enumerate_rawmidi_devices() {
  std::vector<rawmidi_device_row_t> arr;
  glob_t gl;
  memset(&gl, 0, sizeof(gl));
  if (glob("/dev/snd/midiC*", 0, nullptr, &gl) != 0) {
    return arr;
  }
  for (size_t i = 0; i < gl.gl_pathc; ++i) {
    std::string path = gl.gl_pathv[i];
    struct stat st;
    if (stat(path.c_str(), &st) < 0 || !S_ISCHR(st.st_mode)) {
      continue;
    }
    std::string friendly = get_rawmidi_name(path);
    rawmidi_device_row_t row;
    row.type = "rawmidi";
    row.id = path;
    row.device = path;
    row.label = friendly.empty() ? path : friendly;
    row.kind = "rawmidi";
    arr.push_back(std::move(row));
  }
  globfree(&gl);
  return arr;
}



} // namespace rtpmididns

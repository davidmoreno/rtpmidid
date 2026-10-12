/**
 * rtpmidid-lite: one ALSA sequencer port exported over RTP-MIDI.
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

#include "alsa_seq.hpp"
#include "sysex.hpp"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/logger.hpp>
#include <vector>

namespace rtpmidid_lite {

namespace {
/// Big enough for any SysEx, so the encoder never splits one behind our back.
constexpr long MIDI_EVENT_BUFFER_SIZE = 65536;
/// Any decoded non-SysEx event fits comfortably here.
constexpr size_t DECODE_BUFFER_SIZE = 64;

void alsa_error_handler(const char *file, int line, const char *function,
                        int err, const char *fmt, ...) {
  // ALSA reports missing optional files as ENOENT; not useful.
  if (err == ENOENT)
    return;

  std::array<char, 1024> message{};
  va_list args;
  va_start(args, fmt);
  vsnprintf(message.data(), message.size(), fmt, args);
  va_end(args);

  if (err)
    ERROR("ALSA {}:{} {}: {}: {}", file, line, function, message.data(),
          snd_strerror(err));
  else
    ERROR("ALSA {}:{} {}: {}", file, line, function, message.data());
}
} // namespace

alsa_seq_t::alsa_seq_t(const std::string &name) : name_(name) {
  snd_lib_error_set_handler(alsa_error_handler);

  if (snd_seq_open(&seq_, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) <
      0) {
    throw rtpmidid::exception(
        "Can not open the ALSA sequencer. Check the permissions of the user "
        "and that the snd-seq module is loaded.");
  }

  try {
    snd_seq_set_client_name(seq_, name_.c_str());

    // Larger queues and buffers than the defaults, so long SysEx messages are
    // not split or dropped by the kernel sequencer.
    snd_seq_client_pool_t *pool = nullptr;
    snd_seq_client_pool_alloca(&pool);
    if (snd_seq_get_client_pool(seq_, pool) >= 0) {
      snd_seq_client_pool_set_input_pool(pool, 2000);
      snd_seq_client_pool_set_output_pool(pool, 2000);
      snd_seq_set_client_pool(seq_, pool);
    }
    snd_seq_set_input_buffer_size(seq_, 65536);
    snd_seq_set_output_buffer_size(seq_, 65536);

    const auto caps = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE |
                      SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
    const auto type =
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION;

    port_ = snd_seq_create_simple_port(seq_, name_.c_str(), caps, type);
    if (port_ < 0)
      throw rtpmidid::exception("Can not create the ALSA port: {}",
                                snd_strerror(port_));

    client_id_ = snd_seq_client_id(seq_);

    if (snd_midi_event_new(MIDI_EVENT_BUFFER_SIZE, &codec_) < 0)
      throw rtpmidid::exception("Can not create the ALSA MIDI event codec.");

    const auto poller_count = snd_seq_poll_descriptors_count(seq_, POLLIN);
    std::vector<struct pollfd> pfds(static_cast<size_t>(poller_count));
    const auto count =
        snd_seq_poll_descriptors(seq_, pfds.data(), poller_count, POLLIN);
    if (count != poller_count)
      throw rtpmidid::exception(
          "ALSA sequencer poll descriptor count mismatch ({} != {}).", count,
          poller_count);

    for (int i = 0; i < poller_count; i++) {
      listeners_.emplace_back(rtpmidid::poller.add_fd_in(
          pfds[static_cast<size_t>(i)].fd, [this](int) { read_ready(); }));
    }
  } catch (...) {
    listeners_.clear();
    if (codec_)
      snd_midi_event_free(codec_);
    if (seq_)
      snd_seq_close(seq_);
    seq_ = nullptr;
    throw;
  }

  INFO("ALSA port '{}' ready at {}:{}. Use aconnect to wire other ports to it.",
       name_, client_id_, port_);
}

alsa_seq_t::~alsa_seq_t() {
  listeners_.clear();
  if (codec_)
    snd_midi_event_free(codec_);
  if (seq_)
    snd_seq_close(seq_);
}

void alsa_seq_t::send_midi(const rtpmidid::io_bytes_reader &data) {
  // Copy: we advance the position while encoding.
  auto reader = data;
  snd_midi_event_reset_encode(codec_);

  while (reader.position < reader.end) {
    snd_seq_event_t ev{};
    auto used = snd_midi_event_encode(codec_, reader.position,
                                      reader.end - reader.position, &ev);
    if (used <= 0) {
      ERROR("Can not encode MIDI for ALSA, dropping {} bytes.",
            reader.end - reader.position);
      return;
    }
    reader.position += used;

    snd_seq_ev_set_source(&ev, static_cast<unsigned char>(port_));
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);

    auto result = snd_seq_event_output(seq_, &ev);
    if (result < 0) {
      ERROR("Can not send event to ALSA: {}", snd_strerror(result));
      return;
    }
  }

  auto result = snd_seq_drain_output(seq_);
  if (result < 0)
    ERROR("Can not drain the ALSA output queue: {}", snd_strerror(result));
}

void alsa_seq_t::read_ready() {
  snd_seq_event_t *ev = nullptr;
  while (snd_seq_event_input(seq_, &ev) > 0) {
    if (ev == nullptr)
      break;
    handle_event(ev);
  }
}

void alsa_seq_t::handle_event(snd_seq_event_t *ev) {
  switch (ev->type) {
  case SND_SEQ_EVENT_NOTEOFF:
  case SND_SEQ_EVENT_NOTEON:
  case SND_SEQ_EVENT_KEYPRESS:
  case SND_SEQ_EVENT_CONTROLLER:
  case SND_SEQ_EVENT_PGMCHANGE:
  case SND_SEQ_EVENT_CHANPRESS:
  case SND_SEQ_EVENT_PITCHBEND:
  case SND_SEQ_EVENT_CONTROL14:
  case SND_SEQ_EVENT_NONREGPARAM:
  case SND_SEQ_EVENT_REGPARAM:
  case SND_SEQ_EVENT_SONGPOS:
  case SND_SEQ_EVENT_SONGSEL:
  case SND_SEQ_EVENT_QFRAME:
  case SND_SEQ_EVENT_TUNE_REQUEST:
  case SND_SEQ_EVENT_CLOCK:
  case SND_SEQ_EVENT_START:
  case SND_SEQ_EVENT_CONTINUE:
  case SND_SEQ_EVENT_STOP:
  case SND_SEQ_EVENT_SENSING:
  case SND_SEQ_EVENT_RESET: {
    std::array<uint8_t, DECODE_BUFFER_SIZE> buffer{};
    snd_midi_event_reset_decode(codec_);
    auto len = snd_midi_event_decode(codec_, buffer.data(), buffer.size(), ev);
    if (len < 0) {
      ERROR("Can not decode ALSA event type {}: {}", ev->type,
            snd_strerror(len));
      return;
    }
    rtpmidid::io_bytes_reader reader(buffer.data(), static_cast<size_t>(len));
    midi_event(reader);
    return;
  }
  case SND_SEQ_EVENT_SYSEX: {
    std::vector<uint8_t> buffer(ev->data.ext.len + 8);
    snd_midi_event_reset_decode(codec_);
    auto len = snd_midi_event_decode(codec_, buffer.data(), buffer.size(), ev);
    if (len < 0) {
      ERROR("Can not decode ALSA SysEx event: {}", snd_strerror(len));
      return;
    }
    append_sysex(buffer.data(), static_cast<size_t>(len));
    return;
  }
  default:
    // Subscriptions, port start/exit, ticks and everything else is not MIDI.
    return;
  }
}

void alsa_seq_t::append_sysex(const uint8_t *data, size_t len) {
  const auto result = sysex_assembler_.feed(
      data, len, [this](const uint8_t *message, size_t size) {
        emit_sysex(message, size);
      });
  if (result == sysex_feed_result_e::DISCARDED)
    WARNING("Dropped a SysEx fragment: stray continuation or message too "
            "large.");
}

void alsa_seq_t::emit_sysex(const uint8_t *data, size_t len) {
  const bool ok =
      split_sysex(data, len, SYSEX_SEGMENT_BYTES,
                  [this](const uint8_t *segment, size_t size) {
                    auto *mutable_segment = const_cast<uint8_t *>(segment);
                    rtpmidid::io_bytes_reader reader(mutable_segment, size);
                    midi_event(reader);
                  });
  if (!ok)
    WARNING("Ignoring malformed SysEx of {} bytes.", len);
}

} // namespace rtpmidid_lite

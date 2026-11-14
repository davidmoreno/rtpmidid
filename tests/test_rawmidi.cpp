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

// Writing to a [rawmidi] device: the fd is non-blocking and a device drains at
// its own speed (3125 octets per second on a DIN port), so a long SysEx is
// never taken in one write(). The rest must be written as the device accepts
// it, not dropped.

#include "../src/local_rawmidi_peer.hpp"
#include "../src/mididata.hpp"
#include "./test_case.hpp"
#include "./test_utils.hpp"
#include <algorithm>
#include <fcntl.h>
#include <rtpmidid/logger.hpp>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

using rtpmididns::local_rawmidi_peer_t;
using rtpmididns::mididata_t;

using bytes_t = std::vector<uint8_t>;

/// A classic bulk dump: F0, 7 bit data, F7.
static bytes_t make_sysex(size_t data_size) {
  bytes_t ret;
  ret.reserve(data_size + 2);
  ret.push_back(0xF0);
  for (size_t i = 0; i < data_size; i++) {
    ret.push_back(uint8_t(i * 7) & 0x7F);
  }
  ret.push_back(0xF7);
  return ret;
}

/**
 * A pipe with the write end non-blocking, like a raw MIDI device: a write
 * bigger than its buffer is short, and the rest has to wait.
 */
class pipe_t {
public:
  int read_fd = -1;
  int write_fd = -1;

  pipe_t() {
    int fds[2];
    ASSERT_EQUAL(::pipe(fds), 0);
    read_fd = fds[0];
    write_fd = fds[1];
    // Both ends non-blocking: the write end like a raw MIDI device (short
    // writes), the read end so the test never blocks waiting for data.
    for (auto fd : {read_fd, write_fd}) {
      auto flags = fcntl(fd, F_GETFL, 0);
      ASSERT_GTE(flags, 0);
      ASSERT_EQUAL(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
    }
  }
  ~pipe_t() {
    if (read_fd >= 0) {
      ::close(read_fd);
    }
    if (write_fd >= 0) {
      ::close(write_fd);
    }
  }
};

/// Reads everything available, waiting a bit for the daemon to write more.
static void drain(int fd, bytes_t &received) {
  uint8_t buffer[4096];
  while (true) {
    auto ret = ::read(fd, buffer, sizeof(buffer));
    if (ret <= 0) {
      return;
    }
    received.insert(received.end(), buffer, buffer + ret);
  }
}

void test_rawmidi_long_sysex_is_written_completely() {
  const auto sysex = make_sysex(128 * 1024);
  pipe_t pipe;

  local_rawmidi_peer_t peer("test", "/dev/null");
  // The peer is not opened: use the pipe as the device.
  peer.fd = pipe.write_fd;

  peer.send_midi(0,
                 mididata_t(const_cast<uint8_t *>(sysex.data()), sysex.size()));

  // The first write filled the pipe (64 kB by default) and left the rest
  // pending; the peer writes the rest from its timer as we drain.
  bytes_t received;
  for (int i = 0; i < 200 && received.size() < sysex.size(); i++) {
    drain(pipe.read_fd, received);
    poller_wait_for(std::chrono::milliseconds(5));
  }
  drain(pipe.read_fd, received);

  INFO("Sent {} octets, the pipe got {} back", sysex.size(), received.size());
  ASSERT_EQUAL(received.size(), sysex.size());
  ASSERT_TRUE(std::equal(received.begin(), received.end(), sysex.begin()));

  // The peer closes the fd it was given.
  peer.close();
  pipe.write_fd = -1;
}

/// A short message still goes out on the first write, without any timer.
void test_rawmidi_short_message() {
  pipe_t pipe;
  local_rawmidi_peer_t peer("test", "/dev/null");
  peer.fd = pipe.write_fd;

  auto note_on_bin = hex_to_bin("90 40 40");
  bytes_t note_on(note_on_bin.start, note_on_bin.end);
  peer.send_midi(0, mididata_t(note_on.data(), note_on.size()));

  bytes_t received;
  drain(pipe.read_fd, received);
  ASSERT_EQUAL(received.size(), 3);
  ASSERT_TRUE(std::equal(received.begin(), received.end(), note_on.begin()));

  peer.close();
  pipe.write_fd = -1;
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_rawmidi_long_sysex_is_written_completely),
      TEST(test_rawmidi_short_message),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

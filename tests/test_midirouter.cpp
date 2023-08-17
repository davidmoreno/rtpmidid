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
#include "../src/midirouter.hpp"
#include "../tests/test_case.hpp"
#include "midipeer.hpp"
#include "rtpmidid/iobytes.hpp"
#include "test_utils.hpp"
#include <memory>

class rtpmididns::mididata_t : public rtpmidid::io_bytes_reader {
public:
  mididata_t(uint8_t *data, uint32_t size)
      : rtpmidid::io_bytes_reader(data, size) {}
};

class test_midiio_t : public rtpmididns::midipeer_t {
public:
  rtpmidid::io_bytes_managed recv;
  rtpmidid::io_bytes_writer writer;
  test_midiio_t() : recv(1024), writer(recv) {}

  void send_midi(const rtpmididns::mididata_t &data) override {
    writer.copy_from(data);
  }
};

void test_basic_midirouter() {
  rtpmididns::midirouter_t router;
  auto peera = std::make_shared<test_midiio_t>();
  auto peerb = std::make_shared<test_midiio_t>();
  auto peerA = router.add_peer(peera);
  auto peerB = router.add_peer(peerb);

  router.connect(peerA, peerB);
  auto mididata = hex_to_bin("90 64 7F");
  auto data = rtpmididns::mididata_t(mididata.start, mididata.size());

  router.send_midi(peerA, data);

  ASSERT_EQUAL(peera->writer.pos(), 0);

  DEBUG("GOT {} bytes", peerb->writer.pos());
  ASSERT_EQUAL(peerb->writer.pos(), 3);
}

int main(void) {
  test_case_t testcase{
      TEST(test_basic_midirouter),
  };

  testcase.run();

  return testcase.exit_code();
}

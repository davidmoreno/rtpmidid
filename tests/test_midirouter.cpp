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
#include "alsanetwork.hpp"
#include "alsapeer.hpp"
#include "aseq.hpp"
#include "factory.hpp"
#include "midipeer.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidiserver.hpp"
#include "test_utils.hpp"
#include <alsa/seqmid.h>
#include <memory>

class test_midiio_t : public rtpmididns::midipeer_t {
public:
  rtpmidid::io_bytes_managed recv;
  rtpmidid::io_bytes_writer writer;
  test_midiio_t() : recv(1024), writer(recv) {}

  void send_midi(rtpmididns::midipeer_id_t from,
                 const rtpmididns::mididata_t &data) override {
    DEBUG("{} got some data: {}", (void *)this, data.size());
    writer.copy_from(data);
  }
};

// rtpmididns::alsapeer_t::alsapeer_t(const std::string &name,
//                                    rtpmidid::aseq &seq_)
//     : seq(seq_) {}

// rtpmididns::alsapeer_t::~alsapeer_t(){};

// void rtpmididns::alsapeer_t::send_midi(midipeer_id_t from, const mididata_t
// &) {
// }

// rtpmididns::rtpmidiserver_t::rtpmidiserver_t(const std::string &name) {}
// void rtpmididns::rtpmidiserver_t::send_midi(midipeer_id_t from,
//                                             const mididata_t &) {}

std::shared_ptr<rtpmididns::midipeer_t>
rtpmididns::make_rtpmidiserver(const std::string &name) {
  return std::make_shared<test_midiio_t>();
}
std::shared_ptr<rtpmididns::midipeer_t>
rtpmididns::make_alsapeer(const std::string &name, rtpmidid::aseq &seq) {
  return std::make_shared<test_midiio_t>();
}

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

void test_midirouter_from_alsa() {
  rtpmididns::midirouter_t router;
  rtpmididns::alsanetwork_t alsanetwork("test", &router);

  // This must have created a rtpmidid network connection
  auto pair = alsanetwork.new_alsa_connection({128, 0}, "KB01");
  rtpmidid::mididata_to_alsaevents_t mididata_to_alsaevents;
  auto data = rtpmidid::io_bytes_reader(hex_to_bin("90 64 7F"));
  mididata_to_alsaevents.read(data, [&alsanetwork](snd_seq_event_t *ev) {
    DEBUG("Got event!");
    // proper source
    ev->source.client = 128;
    ev->source.port = 0;
    alsanetwork.alsaseq_event(ev);

    // unknown source
    ev->source.client = 120;
    ev->source.port = 0;
    alsanetwork.alsaseq_event(ev);
  });

  test_midiio_t *rtppeer =
      dynamic_cast<test_midiio_t *>(router.peers[pair.second].peer.get());
  // rtppeer->writer.print_hex();
  ASSERT_EQUAL(rtppeer->writer.pos(), 3);
}

int main(void) {
  test_case_t testcase{
      TEST(test_basic_midirouter),
      TEST(test_midirouter_from_alsa),
  };

  testcase.run();

  return testcase.exit_code();
}

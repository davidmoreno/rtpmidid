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

#include "../src/aseq.hpp"
#include "../src/factory.hpp"
#include "../src/json.hpp"
#include "../src/local_alsa_multi_listener.hpp"
#include "../src/local_alsa_peer.hpp"
#include "../src/mididata.hpp"
#include "../src/midipeer.hpp"
#include "../src/midirouter.hpp"
#include "../src/network_rtpmidi_listener.hpp"
#include "../tests/test_case.hpp"
#include "rtpmidid/iobytes.hpp"
#include "test_utils.hpp"
#include <alsa/seq_event.h>
#include <alsa/seqmid.h>
#include <memory>

class test_midiio_t : public rtpmididns::midipeer_t {
public:
  rtpmidid::io_bytes_managed recv;
  rtpmidid::io_bytes_writer writer;
  test_midiio_t() : recv(1024), writer(recv) {}

  void send_midi(rtpmididns::midipeer_id_t from,
                 const rtpmididns::mididata_t &data) override {
    DEBUG("Data at {} to {}", (void *)data.start, (void *)data.end);
    DEBUG("{} got some data: {}", (void *)this, data.size());
    ASSERT_LT(data.start, data.end);
    writer.copy_from(data);
  }

  rtpmididns::json_t status() override { return rtpmididns::json_t{}; }
};

rtpmididns::network_rtpmidi_listener_t::network_rtpmidi_listener_t(
    const std::string &name)
    : server(name, "15005") {}
rtpmididns::network_rtpmidi_listener_t::~network_rtpmidi_listener_t() {}

void rtpmididns::network_rtpmidi_listener_t::send_midi(midipeer_id_t from,
                                                       const mididata_t &) {}
rtpmididns::json_t rtpmididns::network_rtpmidi_listener_t::status() {
  return rtpmididns::json_t{};
}

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
rtpmididns::make_network_rtpmidi_listener(const std::string &name) {
  return std::make_shared<test_midiio_t>();
}
// std::shared_ptr<rtpmididns::midipeer_t>
// rtpmididns::make_local_alsa_peer(const std::string &name,
//                           std::shared_ptr<rtpmidins::aseq_t> seq) {
//   return std::make_shared<test_midiio_t>();
// }

void test_basic_midirouter() {
  auto router = std::make_shared<rtpmididns::midirouter_t>();
  auto peera = std::make_shared<test_midiio_t>();
  auto peerb = std::make_shared<test_midiio_t>();
  auto peerA = router->add_peer(peera);
  auto peerB = router->add_peer(peerb);

  router->connect(peerA, peerB);
  auto mididata = hex_to_bin("90 64 7F");
  auto data = rtpmididns::mididata_t(mididata.start, mididata.size());

  router->send_midi(peerA, data);

  ASSERT_EQUAL(peera->writer.pos(), 0);

  DEBUG("GOT {} bytes", peerb->writer.pos());
  ASSERT_EQUAL(peerb->writer.pos(), 3);
}

void test_midirouter_from_alsa() {
  auto router = std::make_shared<rtpmididns::midirouter_t>();
  std::shared_ptr<rtpmididns::aseq_t> aseq;
  try {
    aseq = std::make_shared<rtpmididns::aseq_t>("Test");
  } catch (rtpmididns::alsa_connect_exception &exc) {
    ERROR("ALSA CONNECT EXCEPTION: {}", exc.what());
    INFO("Skipping test as ALSA is not available.");
    return;
  }

  auto alsanetwork =
      std::make_shared<rtpmididns::local_alsa_multi_listener_t>("test", aseq);
  router->add_peer(alsanetwork);

  // This must have created a rtpmidid network connection
  auto rtpmidinetwork_id =
      alsanetwork->new_alsa_connection({aseq->client_id, 0}, "KB01");
  ASSERT_GT(rtpmidinetwork_id, 0);
  rtpmididns::mididata_to_alsaevents_t mididata_to_alsaevents;
  auto mididata =
      hex_to_bin("90 64 7F"); // Tis must be in a variable to outlive its use
  auto data = rtpmidid::io_bytes_reader(mididata);
  mididata_to_alsaevents.mididata_to_evs_f(
      data, [&alsanetwork, &aseq](snd_seq_event_t *ev) {
        // proper source
        ev->source.client = aseq->client_id;
        ev->source.port = 0;
        // This test the encoder works
        ASSERT_EQUAL(ev->type, SND_SEQ_EVENT_NOTEON);
        alsanetwork->alsaseq_event(ev);

        // unknown source
        ev->source.client = 120;
        ev->source.port = 0;
        alsanetwork->alsaseq_event(ev);
      });

  rtpmididns::midipeer_t *midipeer =
      router->peers[rtpmidinetwork_id].peer.get();
  ASSERT_TRUE(midipeer);
  test_midiio_t *rtppeer = dynamic_cast<test_midiio_t *>(midipeer);
  // rtppeer->writer.print_hex();
  ASSERT_TRUE(rtppeer);
  ASSERT_EQUAL(rtppeer->writer.pos(), 3);
}

void test_midirouter_for_each_peer() {
  auto router = std::make_shared<rtpmididns::midirouter_t>();
  std::shared_ptr<rtpmididns::aseq_t> aseq;
  try {
    aseq = std::make_shared<rtpmididns::aseq_t>("Test");
  } catch (rtpmididns::alsa_connect_exception &exc) {
    ERROR("ALSA CONNECT EXCEPTION: {}", exc.what());
    INFO("Skipping test as ALSA is not available.");
    return;
  }
  auto alsanetwork =
      std::make_shared<rtpmididns::local_alsa_multi_listener_t>("test", aseq);
  auto midiio = std::make_shared<test_midiio_t>();

  router->add_peer(alsanetwork);
  router->add_peer(midiio);

  int count = 0;
  router->for_each_peer<rtpmididns::midipeer_t>(
      [&count](auto peer) { count++; });
  ASSERT_EQUAL(count, 2);

  count = 0;
  router->for_each_peer<test_midiio_t>([&count, &midiio](auto peer) {
    count++;
    ASSERT_EQUAL(peer, midiio.get());
  });
  ASSERT_EQUAL(count, 1);

  count = 0;
  router->for_each_peer<rtpmididns::local_alsa_multi_listener_t>(
      [&count, &alsanetwork](auto peer) {
        count++;

        ASSERT_EQUAL(peer, alsanetwork.get());
      });
  ASSERT_EQUAL(count, 1);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_basic_midirouter),
      TEST(test_midirouter_from_alsa),
      TEST(test_midirouter_for_each_peer),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

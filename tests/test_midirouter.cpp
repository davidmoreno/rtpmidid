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
#include "local_alsa_peer.hpp"
#include "test_utils.hpp"
#include <alsa/seq_event.h>
#include <alsa/seqmid.h>
#include <memory>
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/mdns_rtpmidi.hpp>

namespace rtpmididns {
std::unique_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
}

class test_midiio_t : public rtpmididns::midipeer_t {
public:
  std::string name;
  rtpmidid::io_bytes_managed recv;
  rtpmidid::io_bytes_writer writer;
  test_midiio_t(const std::string &name)
      : name(name), recv(1024), writer(recv) {
    DEBUG("test_midiio_t::test_midiio_t()");
  }

  void send_midi(rtpmididns::midipeer_id_t from,
                 const rtpmididns::mididata_t &data) override {
    DEBUG("Data at {} to {}", (void *)data.start, (void *)data.end);
    DEBUG("{} got some data: {}", (void *)this, data.size());
    ASSERT_LT(data.start, data.end);
    writer.copy_from(data);
  }

  rtpmididns::json_t status() override {
    return rtpmididns::json_t{
        {"name", name},
        {"type", "test_midiio_t"},
        {"recv", recv.pos()},
        {"writer", writer.pos()},
    };
  }
};

std::shared_ptr<rtpmididns::midipeer_t>
make_test_midiio(const std::string &name) {
  return std::make_shared<test_midiio_t>(name);
}

void test_basic_midirouter() {
  auto router = std::make_shared<rtpmididns::midirouter_t>();
  auto peera = std::make_shared<test_midiio_t>("T01");
  auto peerb = std::make_shared<test_midiio_t>("T02");
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
  router->set_debug(true);
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
  auto [alsapeer_id, rtpmidipeer_id] = alsanetwork->new_alsa_connection(
      rtpmididns::aseq_t::connection_t(aseq, {1, 0}, {aseq->client_id, 0}),
      "ALSA LISTENER");
  ASSERT_GT(alsapeer_id, 0);
  ASSERT_GT(rtpmidipeer_id, 0);

  auto replacement_peer = make_test_midiio("MIDI IO");
  DEBUG("Replace peer with id: {} with peer with id: {}", rtpmidipeer_id,
        replacement_peer->peer_id);
  router->replace_peer(rtpmidipeer_id, replacement_peer);

  rtpmididns::mididata_to_alsaevents_t mididata_to_alsaevents;
  auto mididata =
      hex_to_bin("90 64 7F"); // Tis must be in a variable to outlive its use
  auto data = rtpmidid::io_bytes_reader(mididata);

  auto alsapeer =
      router->get_peer_by_id<rtpmididns::local_alsa_peer_t>(alsapeer_id);

  mididata_to_alsaevents.mididata_to_evs_f(
      data, [&alsapeer, &aseq](snd_seq_event_t *ev) {
        ev->source.client = 1;
        ev->source.port = 0;
        ev->dest.client = aseq->client_id;
        ev->dest.port = 0;

        // This test the encoder works
        ASSERT_EQUAL(ev->type, SND_SEQ_EVENT_NOTEON);
        DEBUG("Sending event to alsapeer: {}, from {}:{}", ev->type,
              ev->source.client, ev->source.port);
        alsapeer->alsaseq_event(ev);

        ev->source.client = 1;
        ev->source.port = 0;
        ev->dest.client = aseq->client_id;
        ev->dest.port = 10;

        DEBUG("Sending event to alsapeer: {}, from {}:{}", ev->type,
              ev->source.client, ev->source.port);
        alsapeer->alsaseq_event(ev);
      });

  DEBUG("rtpmidipeer_id: {}", rtpmidipeer_id);
  router->for_each_peer<rtpmididns::midipeer_t>([](auto peer) {
    auto st = peer->status();
    DEBUG("Peer: {} {} {}", peer->peer_id, st["type"], st["name"]);
  });

  auto rtppeer = router->get_peer_by_id<test_midiio_t>(rtpmidipeer_id);

  DEBUG("rtppeer: {}, size: {}", rtppeer->peer_id, rtppeer->writer.pos());
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
  auto midiio = std::make_shared<test_midiio_t>("T01");

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

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

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "../src/config.hpp"
#include "../src/control_socket.hpp"
#include "../src/rtpmidid.hpp"
#include "./aseq.hpp"
#include <alsa/seq.h>

#include "rtpmidid/mdns_rtpmidi.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidid/rtpclient.hpp"
#include "rtpmidid/signal.hpp"
#include "test_case.hpp"

using namespace std::chrono_literals;

static std::vector<std::string> avahi_known_names;

const auto AVAHI_ANNOUNCEMENT_TIMEOUT = 10'000;

rtpmidid::config_t parse_cmd_args(std::vector<const char *> &&list) {
  return rtpmidid::parse_cmd_args(list.size(), list.data());
}

static std::pair<uint8_t, uint8_t>
alsa_find_port(snd_seq_t *seq, snd_seq_client_info_t *cinfo,
               snd_seq_port_info_t *pinfo, int count,
               const std::string &gadgetname, const std::string &portname) {
  auto name = snd_seq_client_info_get_name(cinfo);
  DEBUG("'{}' != '{}'", gadgetname, name);
  if (gadgetname != name) {
    return std::make_pair(0, 0);
  }

  auto pname = snd_seq_port_info_get_name(pinfo);
  DEBUG("'{}' != '{}'", portname, pname);
  if (portname == pname) {
    INFO("GOT IT");
    return std::make_pair(snd_seq_client_info_get_client(cinfo),
                          snd_seq_port_info_get_port(pinfo));
  }

  return std::make_pair(0, 0);
}

static std::pair<uint8_t, uint8_t> print_port_and_subs(
    snd_seq_t *seq, snd_seq_client_info_t *cinfo, snd_seq_port_info_t *pinfo,
    int count, const std::string &gadgetname, const std::string &portname) {
  return alsa_find_port(seq, cinfo, pinfo, count, gadgetname, portname);
}

std::pair<uint8_t, uint8_t> alsa_find_port(rtpmidid::aseq &aseq,
                                           const std::string &gadgetname,
                                           const std::string &portname) {

  snd_seq_client_info_t *cinfo = nullptr;
  snd_seq_port_info_t *pinfo = nullptr;
  int count = 0;

  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);
  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(aseq.seq, cinfo) >= 0) {
    /* reset query info */
    snd_seq_port_info_set_client(pinfo, snd_seq_client_info_get_client(cinfo));
    snd_seq_port_info_set_port(pinfo, -1);
    count = 0;
    while (snd_seq_query_next_port(aseq.seq, pinfo) >= 0) {
      if ((snd_seq_port_info_get_capability(pinfo) &
           (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_READ)) !=
          (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_READ))
        continue;
      auto ret = print_port_and_subs(aseq.seq, cinfo, pinfo, count, gadgetname,
                                     portname);
      if (ret.first) {
        return ret;
      }
      count++;
    }
  }

  return std::make_pair(0, 0);
}

void wait_for_avahi_announcement(const std::string &name) {
  const auto pre = std::chrono::steady_clock::now();
  while (std::find(std::begin(avahi_known_names), std::end(avahi_known_names),
                   name) == std::end(avahi_known_names)) {
    rtpmidid::poller.wait(1s);

    const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - pre)
                             .count();
    if (wait_ms > AVAHI_ANNOUNCEMENT_TIMEOUT) {
      FAIL(FMT::format("Waiting too long for avahi: {}ms", wait_ms));
    }
  }
}

void wait_for_alsa_announcement(const std::string &gadget,
                                const std::string &port) {
  const auto pre = std::chrono::steady_clock::now();

  rtpmidid::aseq aseq("WAIT");

  while (alsa_find_port(aseq, gadget, port).first != 0) {
    rtpmidid::poller.wait(1s);

    const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - pre)
                             .count();
    if (wait_ms > 10'000) {
      FAIL(FMT::format("Waiting too long for avahi: {}ms", wait_ms));
    }
  }
}
class metronome_t {
public:
  rtpmidid::aseq aseq;
  rtpmidid::poller_t::timer_t timer;
  uint8_t port = 0;
  bool paused = false;
  metronome_t(const std::string &gadgetname, const std::string &portname)
      : aseq("metronome") {
    auto gadgetport = alsa_find_port(aseq, gadgetname, portname);
    auto gadget = gadgetport.first;
    auto gport = gadgetport.second;

    port = aseq.create_port("metro");
    auto &seq = aseq.seq;

    snd_seq_addr_t sender, dest;
    snd_seq_port_subscribe_t *subs;
    // int queue = 0, convert_time = 0, convert_real = 0, exclusive = 0;

    snd_seq_client_info_t *info;
    snd_seq_client_info_alloca(&info);
    snd_seq_get_client_info(aseq.seq, info);

    sender.client = snd_seq_client_info_get_client(info);
    sender.port = port;

    dest.client = gadget;
    dest.port = gport;

    DEBUG("Connect {}:{} to {}:{}", sender.client, sender.port, dest.client,
          dest.port);

    snd_seq_port_subscribe_alloca(&subs);
    snd_seq_port_subscribe_set_sender(subs, &sender);
    snd_seq_port_subscribe_set_dest(subs, &dest);
    // snd_seq_port_subscribe_set_queue(subs, queue);
    // snd_seq_port_subscribe_set_exclusive(subs, exclusive);
    // snd_seq_port_subscribe_set_time_update(subs, convert_time);
    // snd_seq_port_subscribe_set_time_real(subs, convert_real);

    if (snd_seq_subscribe_port(seq, subs) < 0) {
      ERROR("Connection failed ({})", snd_strerror(errno));
      FAIL("Connection failed");
      return;
    }

    tick();
  }
  void tick() {
    DEBUG("TICK");
    timer = rtpmidid::poller.add_timer_event(100ms, [this]() { this->tick(); });
    if (paused)
      return;

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_noteon(&ev, 0x90 & 0x0F, 0x90, 0x90);

    snd_seq_ev_set_source(&ev, port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_event_output_direct(aseq.seq, &ev);
  }
};

/**
 * Just logs the events received into a list.
 */
class seq_logger {
public:
  rtpmidid::aseq aseq;
  rtpmidid::poller_t::timer_t timer;
  std::vector<uint8_t> events; // just the type
  uint8_t port;

  seq_logger(const std::string &gadget, const std::string &gport)
      : aseq("logger") {
    port = aseq.create_port("logger");
    auto &seq = aseq.seq;

    snd_seq_addr_t sender, dest;
    snd_seq_port_subscribe_t *subs;
    // int queue = 0, convert_time = 0, convert_real = 0, exclusive = 0;

    snd_seq_client_info_t *info;
    snd_seq_client_info_alloca(&info);
    snd_seq_get_client_info(aseq.seq, info);

    // We have to wait for avahi messages to get here
    auto gadgetport = alsa_find_port(aseq, gadget, gport);
    for (int i = 0; i < 1000; i++) {
      if (gadgetport.first != 0)
        break;
      rtpmidid::poller.wait();
      gadgetport = alsa_find_port(aseq, gadget, gport);
    }
    ASSERT_NOT_EQUAL(gadgetport.first, 0);

    sender.client = gadgetport.first;
    sender.port = gadgetport.second;

    dest.client = snd_seq_client_info_get_client(info);
    dest.port = port;

    DEBUG("Connect {}:{} to {}:{}", sender.client, sender.port, dest.client,
          dest.port);

    snd_seq_port_subscribe_alloca(&subs);
    snd_seq_port_subscribe_set_sender(subs, &sender);
    snd_seq_port_subscribe_set_dest(subs, &dest);
    // snd_seq_port_subscribe_set_queue(subs, queue);
    // snd_seq_port_subscribe_set_exclusive(subs, exclusive);
    // snd_seq_port_subscribe_set_time_update(subs, convert_time);
    // snd_seq_port_subscribe_set_time_real(subs, convert_real);

    if (snd_seq_subscribe_port(seq, subs) < 0) {
      ERROR("Connection failed ({})", snd_strerror(errno));
      FAIL("Connection failed");
      return;
    }
  }

  void poll() {
    snd_seq_event_t *ev;
    int pending;
    while ((pending = snd_seq_event_input(aseq.seq, &ev)) > 0) {
      DEBUG("EVENT!!");
      events.push_back(ev->type);
    }
  }
};

// Waits a little to check we get evetns, and if not, fail.
class ensure_get_ticks {
public:
  bool donewaiting;
  seq_logger logger;

  ensure_get_ticks(const std::string &name, const std::string &port)
      : donewaiting(false), logger(name, port) {}
  void wait() {

    INFO("START WAITING");

    donewaiting = false;
    auto timer = rtpmidid::poller.add_timer_event(400ms, [this]() {
      donewaiting = true;
      DEBUG("STOP WAIT");
    });
    logger.poll();

    auto pre = logger.events.size();
    while (!donewaiting) {
      rtpmidid::poller.wait();
      logger.poll();
    }
    auto post = logger.events.size();

    ASSERT_NOT_EQUAL(pre, post);
    DEBUG("Got {} events", post - pre);
  }
};

struct ServerAB {
  rtpmidid::rtpmidid_t A;
  rtpmidid::rtpmidid_t B;
  connection_t<const std::string &, const std::string &, const std::string &>
      discover_connection;
  connection_t<const std::string &> remote_connection;

  ServerAB()
      : A(parse_cmd_args({"--port", "10000", "--name", "TEST-SERVER-A",
                          "--control", "/tmp/rtpmidid.testA.sock"})),
        B(parse_cmd_args({"--port", "10010", "--name", "TEST-SERVER-B",
                          "--control", "/tmp/rtpmidid.testB.sock"})) {
    avahi_known_names.clear();

    // Keep list of known items by server A
    discover_connection = A.mdns_rtpmidi.discover_event.connect(
        [](const std::string &name, const std::string &address,
           const std::string &port) {
          avahi_known_names.push_back(name);
          DEBUG("Discover {}", name);
        });
    remote_connection =
        A.mdns_rtpmidi.remove_event.connect([](const std::string &name) {
          avahi_known_names.erase(std::find(std::begin(avahi_known_names),
                                            std::end(avahi_known_names), name));
          DEBUG("Undiscover {}", name);
        });

    auto control_A = rtpmidid::control_socket_t(A, "/tmp/rtpmidid.testA.sock");
    auto control_B = rtpmidid::control_socket_t(B, "/tmp/rtpmidid.testB.sock");

    wait_for_avahi_announcement("TEST-SERVER-B");
  }
};

/**
 * Creates real servers and uses control socket to conenct between them
 * sends events and checks they are received. Then connects another client
 * both should receive events. Finally disconnects one and events should
 * still be flowing.
 */
void test_connect_disconnect() {
  auto servers = ServerAB();

  // Create new metronome, uses the poller to just send beats
  DEBUG("Create metronome");
  metronome_t metronome("rtpmidi TEST-SERVER-B", "TEST-SERVER-A");
  ensure_get_ticks("rtpmidi TEST-SERVER-A", "TEST-SERVER-B/metronome-metro")
      .wait();
  INFO("GOT TICKS");
  rtpmidid::poller.wait(100ms);

  // Disconencts, and connect again
  ensure_get_ticks("rtpmidi TEST-SERVER-A", "TEST-SERVER-B/metronome-metro")
      .wait();
  INFO("GOT TICKS");
  rtpmidid::poller.wait(100ms);

  /// Now we connect two at the same time, disconnect one, the other should keep
  /// receiving events
  {
    auto loggera = ensure_get_ticks("rtpmidi TEST-SERVER-A",
                                    "TEST-SERVER-B/metronome-metro");
    {
      auto loggerb = ensure_get_ticks("rtpmidi TEST-SERVER-A",
                                      "TEST-SERVER-B/metronome-metro");
      loggera.wait();
      loggerb.wait();
    }
    loggera.wait();
    loggera.wait();
    {
      auto loggerc = ensure_get_ticks("rtpmidi TEST-SERVER-A",
                                      "TEST-SERVER-B/metronome-metro");
      loggera.wait();
      loggerc.wait();
    }
  }
  rtpmidid::poller.clear_timers();
}

void test_evil_disconnect() {
  auto servers = ServerAB();

  metronome_t metronome("rtpmidi TEST-SERVER-B", "TEST-SERVER-A");

  wait_for_alsa_announcement("TEST-SERVER-B", "metronome-metro");

  ensure_get_ticks loggera = ensure_get_ticks("rtpmidi TEST-SERVER-A",
                                              "TEST-SERVER-B/metronome-metro");
  loggera.wait();

  // Now disconnect the control port of server-b / metronome-metro
  for (auto &peer : servers.B.known_clients) {
    if (peer.peer) {
      DEBUG("Peer: {} / control fd {}", peer.peer->peer.local_name,
            peer.peer->control_socket);
      close(peer.peer->midi_socket);
    }
  }
  loggera.wait();

  rtpmidid::poller.wait();
  rtpmidid::poller.clear_timers();
  rtpmidid::poller.wait(1ms);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_connect_disconnect),
      TEST(test_evil_disconnect),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

#include <bits/stdint-uintn.h>
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
#include "test_case.hpp"

using namespace std::chrono_literals;

std::vector<std::string> avahi_known_names;

rtpmidid::config_t parse_cmd_args(std::vector<const char *> &&list) {
  return rtpmidid::parse_cmd_args(list.size(), list.data());
}

void wait_for_avahi_announcement(const std::string &name) {
  while (std::find(std::begin(avahi_known_names), std::end(avahi_known_names),
                   name) == std::end(avahi_known_names)) {
    rtpmidid::poller.wait();
  }
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

  snd_seq_client_info_t *cinfo;
  snd_seq_port_info_t *pinfo;
  int count;

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

class metronome_t {
public:
  rtpmidid::aseq aseq;
  rtpmidid::poller_t::timer_t timer;
  uint8_t port;
  metronome_t(uint8_t gadget, uint8_t gport) : aseq("metronome") {
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
void ensure_get_ticks(const std::string &name, const std::string &port) {
  seq_logger logger(name, port);

  INFO("START WAITING");

  static bool donewaiting = false;
  auto timer = rtpmidid::poller.add_timer_event(400ms, []() {
    donewaiting = true;
    DEBUG("STOP WAIT");
  });

  while (!donewaiting) {
    DEBUG("W");
    rtpmidid::poller.wait();
    logger.poll();
  }

  ASSERT_NOT_EQUAL(logger.events.size(), 0);
  DEBUG("Got {} events", logger.events.size());
}

/**
 * Creates real servers and uses control socket to conenct between them
 * sends events and checks they are received. Then connects another client
 * both should receive events. Finally disconnects one and events should
 * still be flowing.
 */
void test_connect_disconnect() {
  auto options_A = parse_cmd_args({"--port", "10000", "--name", "TEST-SERVER-A",
                                   "--control", "/tmp/rtpmidid.testA.sock"});

  auto rtpmidid_A = rtpmidid::rtpmidid_t(&options_A);

  // Keep list of known items by server A
  rtpmidid_A.mdns_rtpmidi.discover_event.connect(
      [](const std::string &name, const std::string &address,
         const std::string &port) { avahi_known_names.push_back(name); });
  rtpmidid_A.mdns_rtpmidi.remove_event.connect([](const std::string &name) {
    avahi_known_names.erase(std::find(std::begin(avahi_known_names),
                                      std::end(avahi_known_names), name));
  });

  auto control_A = rtpmidid::control_socket_t(rtpmidid_A, options_A.control);

  auto options_B = parse_cmd_args({"--port", "10010", "--name", "TEST-SERVER-B",
                                   "--control", "/tmp/rtpmidid.testB.sock"});

  auto rtpmidid_B = rtpmidid::rtpmidid_t(&options_B);
  auto control_B = rtpmidid::control_socket_t(rtpmidid_B, options_B.control);

  wait_for_avahi_announcement("TEST-SERVER-B");

  // Connect from SERVER-B to SERVER-A, using "raw" ALSA SEQ, but we use what
  // we have too to avoid creation, descrution and so on
  rtpmidid::aseq aseq("TEST");

  auto alsaport_A_at_B =
      alsa_find_port(aseq, "rtpmidi TEST-SERVER-B", "TEST-SERVER-A");
  DEBUG("PORTS AT {}:{}", alsaport_A_at_B.first, alsaport_A_at_B.second);
  rtpmidid::poller.wait(1ms);

  // Create new metronome, uses the poller to just send beats
  DEBUG("Create metronome");
  metronome_t metronome(alsaport_A_at_B.first, alsaport_A_at_B.second);
  ensure_get_ticks("rtpmidi TEST-SERVER-A", "TEST-SERVER-B/metronome-metro");
  INFO("GOT TICKS");
  rtpmidid::poller.wait();

  // Disconencts, and connect again
  ensure_get_ticks("rtpmidi TEST-SERVER-A", "TEST-SERVER-B/metronome-metro");
  INFO("GOT TICKS");
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_connect_disconnect),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

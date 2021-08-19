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
  rtpmidid::aseq seq;
  rtpmidid::poller_t::timer_t timer;
  metronome_t(uint8_t gadget, uint8_t port) : seq("metronome") { tick(); }
  void tick() {
    DEBUG("TICK");
    timer = rtpmidid::poller.add_timer_event(100ms, [this]() { this->tick(); });
  }
};

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

  // Create new metronome, uses the poller to just send beats
  metronome_t metronome(alsaport_A_at_B.first, alsaport_A_at_B.second);

  for (auto i = 0; i < 30; i++) {
    rtpmidid::poller.wait();
  }
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_connect_disconnect),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

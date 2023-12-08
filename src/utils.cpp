#include "utils.hpp"
#include "json.hpp"
#include "rtpmidid/rtppeer.hpp"

namespace std {
std::string to_string(const rtpmidid::rtppeer_t::status_e status) {
  switch (status) {
  case rtpmidid::rtppeer_t::status_e::NOT_CONNECTED:
    return "NOT_CONNECTED";
  case rtpmidid::rtppeer_t::status_e::CONTROL_CONNECTED:
    return "CONTROL_CONNECTED";
  case rtpmidid::rtppeer_t::status_e::MIDI_CONNECTED:
    return "MIDI_CONNECTED";
  case rtpmidid::rtppeer_t::status_e::CONNECTED:
    return "CONNECTED";
  }
  return "UNKNOWN";
}
} // namespace std

namespace rtpmididns {
json_t peer_status(rtpmidid::rtppeer_t &peer) {
  auto stats = peer.stats.average_and_stddev();
  return json_t{
      //
      {"latency_ms",
       {
           {"last", peer.latency / 10.0},
           {"average", stats.average.count() / 1000.0},
           {"stddev", stats.stddev.count() / 1000.0},
       }},
      {"status", std::to_string(peer.status)},
      {"local",
       {
           {"sequence_number", peer.seq_nr},         //
           {"sequence_number_ack", peer.seq_nr_ack}, //
           {"name", peer.local_name},                //
           {"ssrc", peer.local_ssrc},                //
       }},                                           //
      {
          "remote",
          {
              //
              {"name", peer.remote_name},              //
              {"sequence_number", peer.remote_seq_nr}, //
              {"ssrc", peer.remote_ssrc},              //
              {"port", peer.remote_base_port},         //
              {"hostname", peer.remote_address},       //
          }                                            //
      }
      //
  };
}
} // namespace rtpmididns
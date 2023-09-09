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
json_t peer_status(rtpmidid::rtppeer_t &peer, const std::string &hostname,
                   const std::string &port) {
  return json_t{
      //
      {"latency_ms", peer.latency / 10.0},
      {"status", std::to_string(peer.status)},
      {"sequence_number", peer.seq_nr},
      {"sequence_number_ack", peer.seq_nr_ack},
      {"local",
       {
           {"name", peer.local_name}, {"ssrc", peer.local_ssrc}, //
       }},                                                       //
      {
          "remote",
          {
              //
              {"name", peer.remote_name}, //
              {"ssrc", peer.remote_ssrc}, //
              {"port", port},             //
              {"hostname", hostname},     //
          }                               //
      }
      //
  };
}
} // namespace rtpmididns
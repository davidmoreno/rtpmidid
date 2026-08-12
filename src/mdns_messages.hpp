/// mdns actor messages (design D13; task 6.3): status/announcement
/// commands plus the replies to its create-port/spawn requests. Lives next
/// to `mdns_actor.hpp`.

#pragma once

#include "message_core.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace rtpmididns {

struct mdns_announcement_info_t {
  std::string name;
  int32_t port = 0;
};
struct mdns_remote_info_t {
  std::string name;
  std::string address;
  int32_t port = 0;
};
struct mdns_status_req_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
struct mdns_status_resp_t {
  hdr_t hdr;
  bool available = false;
  std::vector<mdns_announcement_info_t> announcements;
  std::vector<mdns_remote_info_t> remote_announcements;
};
/// Announce/unannounce/remove an rtpmidi service (control-socket users).
struct mdns_announce_t {
  std::string name;
  int32_t port = 0;
};
struct mdns_unannounce_t {
  std::string name;
  int32_t port = 0;
};
struct mdns_remove_t {
  std::string name;
  std::string hostname;
  int32_t port = 0;
};

/// The mdns actor's accepted control messages. No MIDI, no routing,
/// nothing else: status/announcement commands + the replies to its
/// create-port/spawn requests.
using mdns_control_t =
    std::variant<stop_t, mdns_status_req_t, mdns_announce_t, mdns_unannounce_t,
                 mdns_remove_t, peer_ids_result_t, ack_t>;

} // namespace rtpmididns

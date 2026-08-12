/// ALSA actor messages (design D11; task 5.4): hosted-port creation and
/// removal. Lives next to `alsa_actor.hpp`.

#pragma once

#include "message_core.hpp"
#include <string>

namespace rtpmididns {

/// Ask the ALSA actor to create a seq port and register it as a hosted
/// peer; the reply carries the router-assigned id (peer_ids_result_t).
struct alsa_create_port_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  std::string name;
  std::string meta;
};
/// Ask the ALSA actor to remove a hosted port by router id.
struct alsa_remove_port_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t peer_id = 0;
};

/// The ALSA actor's accepted control messages: hosted-port commands (its
/// register acks land on small internal reply mailboxes, not here).
using alsa_control_t =
    std::variant<stop_t, alsa_create_port_t, alsa_remove_port_t>;

} // namespace rtpmididns

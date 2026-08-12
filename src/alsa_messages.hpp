/// ALSA actor messages (design D11; task 5.4): hosted-port creation and
/// removal. Lives next to `alsa_actor.hpp`.

#pragma once

#include "mailbox.hpp"
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
/// ALSA actor -> mdns actor: an ALSA client subscribed to (or left) an
/// announced port; the mdns actor initiates/disconnects the rtpmidi
/// connections to the discovered remotes for that port (the "Network
/// Export" bridge: connecting an ALSA client starts the rtpmidi session).
struct alsa_port_event_t {
  peer_id_t port_id = 0;
  bool subscribed = false;
};

/// The ALSA actor's accepted control messages: hosted-port commands (its
/// register acks land on small internal reply mailboxes, not here).
using alsa_control_t =
    std::variant<stop_t, alsa_create_port_t, alsa_remove_port_t>;
/// The ALSA actor's mailbox (has a data lane: it receives midi_to_wire).
using alsa_mailbox_t = mailbox_t<data_message_t, alsa_control_t>;

} // namespace rtpmididns

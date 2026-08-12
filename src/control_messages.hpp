/// Control-socket and listener messages (spec: control-socket-jsondm): the
/// requester reply/event set a connection actor accepts, and the network/
/// control listeners' control variants. Lives next to
/// `control_socket_actor.hpp` / `network_rtpmidi_listener_actor.hpp`.

#pragma once

#include "mailbox.hpp"
#include "message_core.hpp"
#include "mdns_messages.hpp"
#include "network_messages.hpp"
#include "peer_messages.hpp"
#include "router_messages.hpp"
#include "worker_messages.hpp"

namespace rtpmididns {

/// A control-socket connection actor: the requester reply/event set.
using connection_control_t =
    std::variant<stop_t, ack_t, peer_ids_result_t, status_head_t,
                 peer_status_resp_t, peer_command_resp_t, mdns_status_resp_t,
                 dns_resolved_t, peer_event_t>;
/// A network listener: spawn acks, peer-gone notices, status requests,
/// and datagrams peers re-forward when the SO_REUSEPORT hash misdelivers
/// them (the listener demuxes and routes them to the owning peer).
using listener_control_t = std::variant<stop_t, peer_ids_result_t,
                                        udp_peer_gone_t, peer_status_req_t,
                                        udp_datagram_t>;
/// The control listener: connection exit notices.
using control_listener_control_t = std::variant<stop_t, stopped_t>;
/// A control-socket connection actor's mailbox.
using connection_mailbox_t = mailbox_t<std::monostate, connection_control_t>;
/// A network listener's mailbox.
using listener_mailbox_t = mailbox_t<std::monostate, listener_control_t>;
/// The control listener's mailbox.
using control_listener_mailbox_t =
    mailbox_t<std::monostate, control_listener_control_t>;

} // namespace rtpmididns

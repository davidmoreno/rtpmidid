/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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

/// rtpmidi export server messages (lazy-rtpmidi-connections, design D1/D8):
/// session requests from the ALSA listener, remote/export registry
/// management, listener port control from the server, and the exports
/// status gather. Lives next to `rtpmidi_server_actor.hpp`.

#pragma once

#include "mailbox.hpp"
#include "message_core.hpp"
#include "network_messages.hpp" // udp_datagram_t, udp_peer_gone_t
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace rtpmididns {

/// ALSA listener -> rtpmidi server: the first ALSA client subscribed to a
/// waiting port, or the last one unsubscribed. The server starts/stops the
/// rtpmidi session for the remote accordingly (lazy connections).
struct session_request_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  uint8_t seq_port = 0;   // the listener-owned ALSA port
  peer_id_t port_id = 0;  // router peer id of the port (0 = unregistered)
  std::string remote;     // remote/session identity
  bool subscribed = false; // true = first subscribe, false = last unsubscribe
};

/// ALSA listener reply to create-port / set-registered / subscribe-port
/// requests: the seq port number and (when registered) the router id.
struct alsa_port_result_t {
  hdr_t hdr;
  uint8_t seq_port = 0;
  peer_id_t peer_id = 0; // 0 while unregistered
};

/// rtpmidi server -> ALSA listener: a port now has / no longer has a live
/// session wired to it. The listener keeps a session port registered for
/// the lifetime of the session (even with zero ALSA subscribers) and
/// unregisters it on session end when nobody is subscribed.
struct alsa_port_session_t {
  uint8_t seq_port = 0;
  bool has_session = false;
};

/// rtpmidi server -> ALSA listener: register or unregister a waiting port
/// (inbound reuse keeps the port registered while the session is live;
/// session end unregisters it when it has no ALSA subscribers). The
/// listener enforces the subscriber-count rules and replies with the
/// resulting router id (`alsa_port_result_t`).
struct alsa_port_set_registered_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  uint8_t seq_port = 0;
  bool registered = false;
};

/// mdns -> server: a discovered rtpmidi server (an outbound target). The
/// server records the target; the mdns actor separately asks the ALSA
/// listener for the waiting port.
struct server_remote_discovered_t {
  std::string remote; // session identity / ALSA port name
  std::string address;
  std::string port;
};
/// mdns -> server: the seq port number of a remote's waiting port (the
/// mdns actor creates the port; the server needs the number for inbound
/// reuse).
struct server_remote_port_t {
  std::string remote;
  uint8_t seq_port = 0;
};
/// mdns -> server: a previously discovered remote is gone.
struct server_remote_gone_t {
  std::string remote;
};

/// main -> server: a `[connect_to]` target (lazy: the waiting ALSA port is
/// created by the server through the listener; no session until subscribed).
struct server_connect_to_t {
  std::string remote; // session identity / ALSA port name
  std::string hostname;
  std::string port;
  std::string local_udp_port;
};

/// Kinds of exported endpoints on the rtpmidi server.
enum class export_kind_e : uint8_t { network, rawmidi, seq };
inline std::string to_string(export_kind_e kind) {
  switch (kind) {
  case export_kind_e::network:
    return "network";
  case export_kind_e::rawmidi:
    return "rawmidi";
  case export_kind_e::seq:
    return "seq";
  }
  return "?";
}

/// Register an export: listen socket pair (control P / midi P+1) + mDNS
/// announcement + registry entry. For rawmidi exports nothing is opened.
struct export_add_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  std::string name;
  export_kind_e kind = export_kind_e::network;
  std::string target; // rawmidi device path / seq "client:port"
  uint16_t port = 0;  // requested control listen port (0 = ephemeral)
};
/// Remove an export: sessions on it are closed, sockets closed, unannounced.
struct export_remove_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  std::string name;
};

/// Client-mode rawmidi (`hostname=` set): the server opens the device
/// eagerly, spawns the rawmidi peer and an outbound network client to
/// `hostname:remote_udp_port`, and wires them.
struct server_rawmidi_client_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  std::string name;
  std::string device;
  std::string hostname;
  std::string remote_udp_port;
  std::string local_udp_port;
};

/// Status gather for the control socket `exports` section: the listener
/// reports waiting ports (+ targets/states); the server reports its export
/// inventory. Both answer the same requester with `exports_status_resp_t`.
struct exports_status_req_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
struct export_status_entry_t {
  std::string name;
  std::string kind;   // "network" | "rawmidi" | "seq" | "waiting"
  std::string target; // remote address / device path / local port
  std::string state;  // "waiting" | "subscribed" | "connected" | "listening"
  int port = 0;
};
struct exports_status_resp_t {
  hdr_t hdr;
  std::string source; // "alsa" | "rtpmidi_server"
  std::vector<export_status_entry_t> exports;
};

/// The rtpmidi server actor's accepted control messages: session requests,
/// remote/export registry management, the exports status gather, and the
/// replies/routed-datagram lane it shares with its spawned peers (spawn
/// acks, connect acks, peer-gone notices, re-forwarded datagrams).
using server_control_t =
    std::variant<stop_t, session_request_t, server_remote_discovered_t,
                 server_remote_port_t, server_remote_gone_t,
                 server_connect_to_t,
                 server_rawmidi_client_t, export_add_t, export_remove_t,
                 exports_status_req_t, peer_ids_result_t, alsa_port_result_t,
                 ack_t, peer_event_t, udp_peer_gone_t, udp_datagram_t>;
/// The rtpmidi server actor's mailbox.
using server_mailbox_t = mailbox_t<std::monostate, server_control_t>;

} // namespace rtpmididns

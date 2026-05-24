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
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rtpmididns {

// [dm-json]
struct latency_sample_t {
  double last;
  double average;
  double stddev;
};

// [dm-json]
struct internal_latency_ms_t {
  latency_sample_t until_send_midi_ms;
  latency_sample_t send_midi_ms;
};

// [dm-json]
struct peer_stats_t {
  uint64_t recv;
  uint64_t sent;
};

// [dm-json]
struct rtp_endpoint_status_t {
  std::string name;
  std::string hostname;
  uint16_t port;
  uint32_t ssrc;
  uint16_t sequence_number;
  std::optional<uint16_t> sequence_number_ack; // [dm-json: omit_if_null]
};

// [dm-json]
struct rtp_peer_status_t {
  latency_sample_t latency_ms;
  std::string status;
  rtp_endpoint_status_t local;
  rtp_endpoint_status_t remote;
};

// [dm-json]
struct alsa_subscribe_from_t {
  int32_t client;
  int32_t port;
  std::string client_name;
  std::string port_name;
};

// [dm-json]
struct listener_endpoint_t {
  std::string hostname;
  std::string port;
};

// [dm-json]
struct alsa_connection_item_t {
  std::string alsa;
  std::string local;
};

// [dm-json]
struct alsa_subscription_row_t {
  int32_t from_client;
  int32_t from_port;
  int32_t to_client;
  int32_t to_port;
  std::string from_label;
  std::string to_label;
};

// [dm-json]
struct listening_ports_t {
  std::string name;
  uint16_t control_port;
  uint16_t midi_port;
};

/** One router table row: flat merge of router metadata + peer status fields.
 *  Router list sets id/type/send_to/stats/internal_latency_ms; peer-only status omits those keys.
 */
// [dm-json]
struct router_peer_row_t {
  std::optional<uint64_t> id;                                           // [dm-json: omit_if_null]
  std::optional<std::string> type;                                       // [dm-json: omit_if_null]
  std::optional<std::vector<uint32_t>> send_to;                         // [dm-json: omit_if_null]
  std::optional<peer_stats_t> stats;                                     // [dm-json: omit_if_null]
  std::optional<internal_latency_ms_t> internal_latency_ms;            // [dm-json: omit_if_null]

  std::optional<std::string> name;                                      // [dm-json: omit_if_null]
  std::optional<int32_t> port;                                         // [dm-json: omit_if_null]
  std::optional<alsa_subscribe_from_t> alsa_subscribe_from;              // [dm-json: omit_if_null]
  std::optional<std::vector<listener_endpoint_t>> endpoints;             // [dm-json: omit_if_null]
  std::optional<int32_t> connection_count;                               // [dm-json: omit_if_null]
  std::optional<std::string> status;                                     // [dm-json: omit_if_null]
  std::optional<std::string> device;                                     // [dm-json: omit_if_null]
  std::optional<std::vector<alsa_connection_item_t>> connections;      // [dm-json: omit_if_null]
  std::optional<rtp_peer_status_t> peer;                                 // [dm-json: omit_if_null]
  std::optional<std::vector<rtp_peer_status_t>> peers;                   // [dm-json: omit_if_null]
  std::optional<std::string> connect_hostname;                           // [dm-json: omit_if_null]
  std::optional<std::string> connect_port;                               // [dm-json: omit_if_null]
  std::optional<listening_ports_t> listening;                            // [dm-json: omit_if_null]
  std::optional<std::string> error;                                      // [dm-json: omit_if_null]
  /** Web UI MIDI monitor sink (`webui_midi_monitor_peer_t`). */
  std::optional<std::string> monitor_uuid;                                // [dm-json: omit_if_null]
  std::optional<uint64_t> monitor_target_peer_id;                         // [dm-json: omit_if_null]
};

// [dm-json]
struct mdns_announce_row_t {
  std::string name;
  uint32_t port;
};

// [dm-json]
struct mdns_remote_row_t {
  std::string name;
  std::string hostname;
  std::string ip;
  uint32_t port;
};

// [dm-json]
struct mdns_snapshot_t {
  std::string status;
  std::vector<mdns_announce_row_t> announcements; // [dm-json: omit_if_empty]
  std::vector<mdns_remote_row_t> remote_announcements; // [dm-json: omit_if_empty]
};

// [dm-json]
struct settings_status_t {
  std::string alsa_name;
  std::string control_filename;
};

// [dm-json]
struct daemon_status_t {
  std::string version;
  settings_status_t settings;
  std::vector<router_peer_row_t> router;
  mdns_snapshot_t mdns;
};

// [dm-json]
struct alsa_seq_port_row_t {
  std::string type;
  std::string id;
  int32_t client;
  int32_t port;
  std::string client_name;
  std::string port_name;
  std::string label;
  std::string kind;
};

// [dm-json]
struct rawmidi_device_row_t {
  std::string type;
  std::string id;
  std::string device;
  std::string label;
  std::string kind;
};

} // namespace rtpmididns

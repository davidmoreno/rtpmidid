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

#include "dm_json_status.hpp"

namespace rtpmididns {

/** Generic `{"error":"..."}` body used in inline error results. */
// [dm-json]
struct rpc_error_body_t {
  std::string error;
};

/**
 * JSON-RPC success envelope: {"id": <raw>, "result": <raw>}.
 * Both fields are opaque — they carry pre-serialised JSON verbatim.
 */
// [dm-json]
struct rpc_result_t {
  std::string id;     // [dm-json: opaque]
  std::string result; // [dm-json: opaque]
};

/**
 * JSON-RPC error envelope: {"id": <raw>, "error": "..."}.
 * id is opaque (raw JSON); error is a plain string.
 */
// [dm-json]
struct rpc_error_envelope_t {
  std::string id; // [dm-json: opaque]
  std::string error;
};

// [dm-json]
struct rpc_help_entry_t {
  std::string name;
  std::string description;
};

// [dm-json]
struct router_remove_params_t {
  uint64_t peer_id;
};

// [dm-json]
struct router_connect_params_t {
  uint64_t from;
  uint64_t to;
};

// [dm-json]
struct connect_params_t {
  std::optional<std::string> name; // [dm-json: omit_if_null]
  std::string hostname;
  std::optional<std::string> port; // [dm-json: omit_if_null] default 5004 in handler
};

// [dm-json]
struct mdns_remove_params_t {
  std::string name;
  std::optional<std::string> hostname; // [dm-json: omit_if_null]
  int32_t port;
};

// [dm-json]
struct export_rawmidi_error_t {
  std::string error;
  std::map<std::string, std::string> params;
};

// [dm-json]
struct export_rawmidi_params_t {
  std::string device;
  std::optional<std::string> name;            // [dm-json: omit_if_null]
  std::optional<std::string> local_udp_port;   // [dm-json: omit_if_null]
  std::optional<std::string> remote_udp_port;  // [dm-json: omit_if_null]
  std::optional<std::string> hostname;         // [dm-json: omit_if_null]
};

// [dm-json]
struct create_local_rawmidi_params_t {
  std::string name;
  std::string device;
};

// [dm-json]
struct create_network_rtpmidi_client_params_t {
  std::string name;
  std::string hostname;
  std::string port;
};

// [dm-json]
struct create_network_rtpmidi_listener_params_t {
  std::string name;
  uint16_t udp_port;
};

// [dm-json]
struct create_local_alsa_peer_params_t {
  std::string name;
  std::optional<int32_t> alsa_client; // [dm-json: omit_if_null]
  std::optional<int32_t> alsa_port;   // [dm-json: omit_if_null]
};

/** router.create.list — nested type name -> field -> description */
// [dm-json]
struct router_create_list_t {
  std::map<std::string, std::map<std::string, std::string>> schemas;
};

// [dm-json]
struct listener_add_endpoint_params_t {
  std::string hostname;
  std::string port;
};

// [dm-json]
struct listener_remove_endpoint_params_t {
  std::string hostname;
  std::string port;
};

// [dm-json]
struct listener_help_entry_t {
  std::string name;
  std::string description;
};

// [dm-json]
struct ws_auth_params_t {
  std::string username;
  std::string password;
};

// [dm-json]
struct endpoint_connect_params_t {
  std::string from;
  std::string to;
  std::optional<bool> bidi; // [dm-json: omit_if_null] default true in handler
};

// [dm-json]
struct endpoint_disconnect_params_t {
  std::string from;
  std::string to;
};

// [dm-json]
struct monitor_start_params_t {
  /** Same endpoint id strings as `endpoint.connect` / Devices tab (`alsa:…`, `raw:…`, `mdns:…`). */
  std::string endpoint;
};

// [dm-json]
struct monitor_start_result_t {
  std::string uuid;
  uint64_t peer_id;
  uint64_t target_peer_id;
};

// [dm-json]
struct monitor_stop_params_t {
  std::string uuid;
};

// [dm-json]
struct persisted_connection_row_t {
  std::string side_a;
  std::string side_b;
  std::string direction = "both"; // a2b | b2a | both
  int32_t enabled = 1;
  int32_t active_a = 0;
  int32_t active_b = 0;
  std::optional<uint64_t> peer_a; // [dm-json: omit_if_null]
  std::optional<uint64_t> peer_b; // [dm-json: omit_if_null]
};

// [dm-json]
struct connections_list_result_t {
  int32_t enabled = 0;
  std::vector<persisted_connection_row_t> connections;
};

// [dm-json]
struct connections_mutate_params_t {
  std::string side_a;
  std::string side_b;
};

// [dm-json]
struct connections_save_params_t {
  std::string side_a;
  std::string side_b;
  std::string direction = "both"; // a2b | b2a | both
  int32_t enabled = 1;
};

// [dm-json]
struct connections_enable_params_t {
  std::string side_a;
  std::string side_b;
};

// [dm-json]
struct device_list_row_t {
  std::string identity;
  std::string type;
  std::string name;
  std::string source; // discovered | ini | manual
  int64_t first_seen = 0;
  int64_t last_seen = 0;
  int32_t online = 0;
  std::optional<uint64_t> peer_id; // [dm-json: omit_if_null]
};

// [dm-json]
struct devices_list_result_t {
  int32_t enabled = 0;
  std::vector<device_list_row_t> devices;
};

// [dm-json]
struct devices_add_manual_params_t {
  std::string identity;
  std::optional<std::string> name; // [dm-json: omit_if_null]
};

// [dm-json]
struct devices_remove_params_t {
  std::string identity;
};

} // namespace rtpmididns

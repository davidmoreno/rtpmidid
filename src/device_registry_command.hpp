/**
 * Typed messages for the device registry actor thread.
 */
#pragma once

#include "device_query.hpp"
#include "midipeer.hpp"
#include "rtpmidid/reply_channel.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rtpmididns {

enum class device_source_e { discovered, ini, manual };

struct seed_ini_t {
  std::vector<device_identity_t> devices;
};

struct observe_peer_t {
  peer_id_t peer_id = 0;
  device_source_e source = device_source_e::discovered;
};

struct mark_offline_t {
  peer_id_t peer_id = 0;
};

struct add_manual_t {
  device_identity_t identity;
  std::string display_name;
};

struct remove_manual_t {
  std::string identity_key;
  rtpmidid::reply_slot_t reply;
};

struct refresh_from_router_t {};

struct sweep_stale_t {
  int64_t max_age_seconds = 0;
  rtpmidid::reply_slot_t reply;
};

struct set_referenced_provider_t {
  std::function<std::vector<device_query_t>()> provider;
};

struct query_list_devices_t {
  rtpmidid::reply_slot_t reply;
};

struct query_find_by_key_t {
  std::string identity_key;
  rtpmidid::reply_slot_t reply;
};

struct shutdown_t {};

using device_registry_command_t =
    std::variant<seed_ini_t, observe_peer_t,
                 mark_offline_t, add_manual_t,
                 remove_manual_t,
                 refresh_from_router_t,
                 sweep_stale_t,
                 set_referenced_provider_t,
                 query_list_devices_t,
                 query_find_by_key_t, shutdown_t>;

} // namespace rtpmididns

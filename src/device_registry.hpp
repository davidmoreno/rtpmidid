/**
 * Phase 4: device registry — merged device list with online/offline tracking.
 */
#pragma once

#include "device_identity.hpp"
#include "device_db.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/signal.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace rtpmididns {

class device_db_t;
struct settings_t;

enum class device_source_e { discovered, ini, manual };

struct device_record_t {
  device_identity_t identity;
  device_source_e source = device_source_e::discovered;
  std::string display_name;
  int64_t first_seen = 0;
  int64_t last_seen = 0;
  std::optional<peer_id_t> online_peer_id;

  bool online() const { return online_peer_id.has_value(); }
  std::string identity_key() const { return identity.serialize(); }
};

/** Build INI-configured device identities before peers come online. */
std::vector<device_identity_t>
ini_device_identities_from_settings(const settings_t &settings);

class device_registry_t {
  NON_COPYABLE_NOR_MOVABLE(device_registry_t)

public:
  device_registry_t(std::shared_ptr<midirouter_t> router,
                    std::unique_ptr<device_db_t> db = nullptr);
  ~device_registry_t() = default;

  void attach();
  void seed_ini(const std::vector<device_identity_t> &devices);
  void refresh_from_router();

  void add_manual(device_identity_t identity, std::string display_name = {});
  bool remove_manual(const std::string &identity_key);

  std::vector<device_record_t> list_devices() const;
  std::optional<device_record_t>
  find_by_identity_key(const std::string &key) const;

  rtpmidid::signal_t<> changed_event;

private:
  std::shared_ptr<midirouter_t> router_;
  std::unique_ptr<device_db_t> db_;
  mutable std::mutex mutex_;
  std::map<std::string, device_record_t> devices_;
  std::map<peer_id_t, std::string> peer_to_identity_;

  rtpmidid::connection_t<peer_id_t, peer_id_t> connected_connection_;
  rtpmidid::connection_t<peer_id_t, peer_id_t> disconnected_connection_;
  rtpmidid::connection_t<peer_id_t> peer_added_connection_;
  rtpmidid::connection_t<peer_id_t> peer_removed_connection_;
  rtpmidid::connection_t<peer_id_t, midipeer_event_e> peer_event_connection_;

  static int source_priority(device_source_e source);
  void upsert_locked(device_record_t record, bool persist);
  void notify_changed();
  void observe_peer(peer_id_t peer_id, device_source_e source);
  void mark_offline_locked(peer_id_t peer_id);

  void on_connected(peer_id_t from, peer_id_t to);
  void on_disconnected(peer_id_t from, peer_id_t to);
  void on_peer_added(peer_id_t peer_id);
  void on_peer_removed(peer_id_t peer_id);
  void on_peer_event(peer_id_t peer_id, midipeer_event_e evt);
};

} // namespace rtpmididns

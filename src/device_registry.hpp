/**
 * Phase 4: device registry — merged device list with online/offline tracking.
 */
#pragma once

#include "device_identity.hpp"
#include "device_db.hpp"
#include "device_query.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidid/signal.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
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

/** Default age after which an unreferenced discovered device is prunable. */
constexpr int64_t kDefaultStaleDeviceSeconds = 30 * 24 * 60 * 60; // ~1 month

/**
 * Identity keys of discovered devices that are offline, older than @a
 * cutoff_last_seen, and not matched by any referenced query. Pure helper
 * (no I/O) so the retention rules are unit-testable in isolation.
 */
std::vector<std::string> select_stale_discovered_devices(
    const std::vector<device_record_t> &devices,
    const std::vector<device_query_t> &referenced_queries,
    int64_t cutoff_last_seen);

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

  /** Source of queries that protect a device from pruning (Phase 8). */
  using referenced_queries_fn = std::function<std::vector<device_query_t>()>;
  void set_referenced_queries_provider(referenced_queries_fn provider);

  /** Prune stale, unreferenced discovered devices. Returns count removed. */
  size_t sweep_stale_discovered(int64_t max_age_seconds);

  /** Schedule a self-rescheduling cleanup sweep on the poller. */
  void start_periodic_cleanup(std::chrono::seconds interval,
                              int64_t max_age_seconds);

  rtpmidid::signal_t<> changed_event;

private:
  std::shared_ptr<midirouter_t> router_;
  std::unique_ptr<device_db_t> db_;
  mutable std::mutex mutex_;
  std::map<std::string, device_record_t> devices_;
  std::map<peer_id_t, std::string> peer_to_identity_;

  referenced_queries_fn referenced_queries_provider_;
  rtpmidid::poller_t::timer_t cleanup_timer_;
  std::chrono::seconds cleanup_interval_{0};
  int64_t cleanup_max_age_seconds_ = 0;
  void schedule_cleanup();

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

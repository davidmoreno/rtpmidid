/**
 * Phase 4: device registry — merged device list with online/offline tracking.
 */
#pragma once

#include "device_identity.hpp"
#include "device_db.hpp"
#include "device_query.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/blocking_priority_queue.hpp"
#include "rtpmidid/reply_channel.hpp"
#include "rtpmidid/signal.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "device_registry_command.hpp"

namespace rtpmididns {

class connection_db_manager_t;
class device_db_t;
class device_registry_t;
struct settings_t;

const char *device_source_to_wire(device_source_e source);
std::optional<device_source_e> device_source_from_wire(const char *wire);

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

/** Queries from stored connections that protect devices from pruning. */
std::vector<device_query_t>
referenced_queries_from(const connection_db_manager_t &conn);

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

class cron_tasks_t;

/** Register periodic stale-device sweep on the cron thread. */
void schedule_device_registry_cleanup(
    cron_tasks_t &cron, const std::shared_ptr<device_registry_t> &registry,
    std::chrono::seconds interval, int64_t max_age_seconds);

class device_registry_t {
  NON_COPYABLE_NOR_MOVABLE(device_registry_t)

public:
  using referenced_queries_fn = std::function<std::vector<device_query_t>()>;

  device_registry_t(std::shared_ptr<midirouter_t> router,
                    std::unique_ptr<device_db_t> db = nullptr);
  ~device_registry_t();

  void start_registry_thread();
  void stop_registry_thread();

  void attach();
  void seed_ini(const std::vector<device_identity_t> &devices);
  void refresh_from_router();

  void add_manual(device_identity_t identity, std::string display_name = {});

  /** Remove any device from the registry (not limited to manual source).
   *  Returns true if the device was found and removed. */
  bool remove_device(const std::string &identity_key);

  std::vector<device_record_t> list_devices() const;
  std::optional<device_record_t>
  find_by_identity_key(const std::string &key) const;

  void set_referenced_queries_provider(referenced_queries_fn provider);

  /** Prune stale, unreferenced discovered devices. Returns count removed. */
  size_t sweep_stale_discovered(int64_t max_age_seconds);

  /** Immediately remove all offline discovered devices that are NOT
   *  referenced by any saved connection. Returns count removed. */
  size_t prune_unreferenced_offline();

  rtpmidid::signal_t<> changed_event;

private:
  std::shared_ptr<midirouter_t> router_;
  std::unique_ptr<device_db_t> db_;
  std::map<std::string, device_record_t> devices_;
  std::map<peer_id_t, std::string> peer_to_identity_;

  referenced_queries_fn referenced_queries_provider_;

  rtpmidid::blocking_priority_queue<device_registry_command_t,
                                    /* High */ 64,
                                    /* Normal */ 256,
                                    /* Low */ 64>
      queue_;
  std::thread registry_thread_;
  std::atomic<bool> registry_running_{false};

  rtpmidid::connection_t<peer_id_t, peer_id_t> connected_connection_;
  rtpmidid::connection_t<peer_id_t, peer_id_t> disconnected_connection_;
  rtpmidid::connection_t<peer_id_t> peer_added_connection_;
  rtpmidid::connection_t<peer_id_t> peer_removed_connection_;
  rtpmidid::connection_t<peer_id_t, midipeer_event_e> peer_event_connection_;

  void registry_thread_loop();
  bool on_registry_thread() const;
  bool sync_mode() const;
  bool enqueue(device_registry_command_t &&cmd,
               rtpmidid::queue_priority_e prio);

  template <typename T, typename Cmd>
  T dispatch_query(Cmd proto, rtpmidid::queue_priority_e prio,
                   std::function<T(device_registry_t &)> sync_fn) const;

  void handle(seed_ini_t &cmd);
  void handle(observe_peer_t &cmd);
  void handle(mark_offline_t &cmd);
  void handle(add_manual_t &cmd);
  void handle(remove_device_t &cmd);
  void handle(refresh_from_router_t &cmd);
  void handle(sweep_stale_t &cmd);
  void handle(prune_unreferenced_offline_t &cmd);
  void handle(set_referenced_provider_t &cmd);
  void handle(query_list_devices_t &cmd);
  void handle(query_find_by_key_t &cmd);
  void handle(shutdown_t &cmd);

  void upsert_impl(device_record_t record, bool persist);
  void notify_changed();
  void observe_peer_impl(peer_id_t peer_id, device_source_e source);

  /** Mark a peer offline and immediately prune the device if unreferenced. */
  void mark_offline_impl(peer_id_t peer_id);
  bool remove_device_impl(const std::string &identity_key);
  std::vector<device_record_t> list_devices_impl();
  size_t sweep_stale_impl(int64_t max_age_seconds);
  size_t prune_unreferenced_offline_impl();

  void on_connected(peer_id_t from, peer_id_t to);
  void on_disconnected(peer_id_t from, peer_id_t to);
  void on_peer_added(peer_id_t peer_id);
  void on_peer_removed(peer_id_t peer_id);
  void on_peer_event(peer_id_t peer_id, midipeer_event_e evt);

  static constexpr std::chrono::milliseconds kReplyTimeout{5000};
};

template <typename T, typename Cmd>
T device_registry_t::dispatch_query(
    Cmd proto, rtpmidid::queue_priority_e prio,
    std::function<T(device_registry_t &)> sync_fn) const {
  if (sync_mode() || on_registry_thread()) {
    return sync_fn(const_cast<device_registry_t &>(*this));
  }
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  const uint64_t id = channel->next_id();
  proto.reply = rtpmidid::reply_slot_t{channel, id};
  if (!const_cast<device_registry_t *>(this)->enqueue(
          device_registry_command_t{std::move(proto)}, prio)) {
    return T{};
  }
  auto env = channel->wait(id, kReplyTimeout);
  if (!env.error.empty()) {
    return T{};
  }
  try {
    return std::any_cast<T>(env.value);
  } catch (const std::bad_any_cast &) {
    return T{};
  }
}

} // namespace rtpmididns

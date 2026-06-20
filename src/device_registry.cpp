/**
 * Phase 4: device registry actor implementation.
 */
#include "device_registry.hpp"

#include "connection_db.hpp"
#include "cron_tasks.hpp"
#include "device_db.hpp"
#include "device_identity_from_peer.hpp"
#include "settings.hpp"
#include "time_utils.hpp"

#include "rtpmidid/logger.hpp"
#include "rtpmidid/shutdown_signals.hpp"
#include <algorithm>
#include <cstring>
#include <utility>
#include <variant>

namespace rtpmididns {

namespace {

thread_local device_registry_t *g_current_registry_thread = nullptr;

std::string display_name_for(const device_identity_t &identity,
                             const std::optional<std::string> &row_name) {
  if (row_name && !row_name->empty())
    return *row_name;
  for (const char *key : {"name", "client", "service"}) {
    if (const auto v = identity.find(key))
      return *v;
  }
  return identity.serialize();
}

std::optional<router_peer_row_t> status_row_for_peer(const midirouter_t &router,
                                                     peer_id_t peer_id) {
  for (const auto &row : router.status_rows()) {
    if (row.id && static_cast<peer_id_t>(*row.id) == peer_id)
      return row;
  }
  return std::nullopt;
}

/** Check whether @a identity is referenced by any saved connection query.
 *  Canonicalizes both sides (ephemeral fields like port numbers stripped)
 *  so the same physical device is matched regardless of port changes. */
static bool is_referenced(
    const device_identity_t &identity,
    const std::vector<device_query_t> &referenced_queries) {
  const auto canonical_dev = device_identity_t::parse(identity.canonical_key());
  if (!canonical_dev)
    return false;
  for (const auto &query : referenced_queries) {
    // Strip ephemeral fields from the query side too
    const auto query_identity = device_identity_t::parse(query.serialize());
    if (!query_identity)
      continue;
    const auto canonical_q =
        device_identity_t::parse(query_identity->canonical_key());
    if (!canonical_q)
      continue;
    // Build a temporary query from the canonical form
    device_query_t cq;
    cq.type_prefix = canonical_q->type_prefix;
    cq.fields = canonical_q->fields;
    if (cq.matches(*canonical_dev))
      return true;
  }
  return false;
}

bool is_stale_discovered(const device_record_t &record,
                         const std::vector<device_query_t> &referenced_queries,
                         int64_t cutoff_last_seen) {
  if (record.source != device_source_e::discovered)
    return false;
  if (record.online())
    return false;
  if (record.last_seen >= cutoff_last_seen)
    return false;
  return !is_referenced(record.identity, referenced_queries);
}

void post_reply(const rtpmidid::reply_slot_t &slot, auto &&value) {
  if (!slot.wants_reply())
    return;
  rtpmidid::reply_envelope_t env;
  env.id = slot.id;
  env.value = std::forward<decltype(value)>(value);
  slot.channel->post(std::move(env));
}

} // namespace

const char *device_source_to_wire(device_source_e source) {
  switch (source) {
  case device_source_e::discovered:
    return "discovered";
  case device_source_e::ini:
    return "ini";
  case device_source_e::manual:
    return "manual";
  }
  return "discovered";
}

std::optional<device_source_e> device_source_from_wire(const char *wire) {
  if (!wire)
    return std::nullopt;
  if (std::strcmp(wire, "discovered") == 0)
    return device_source_e::discovered;
  if (std::strcmp(wire, "ini") == 0)
    return device_source_e::ini;
  if (std::strcmp(wire, "manual") == 0)
    return device_source_e::manual;
  return std::nullopt;
}

std::vector<std::string> select_stale_discovered_devices(
    const std::vector<device_record_t> &devices,
    const std::vector<device_query_t> &referenced_queries,
    int64_t cutoff_last_seen) {
  std::vector<std::string> stale;
  for (const auto &record : devices) {
    if (is_stale_discovered(record, referenced_queries, cutoff_last_seen))
      stale.push_back(record.identity_key());
  }
  return stale;
}

std::vector<device_query_t>
referenced_queries_from(const connection_db_manager_t &conn) {
  std::vector<device_query_t> out;
  for (const auto &c : conn.database().list_connections()) {
    if (auto q = device_query_t::parse(c.side_a))
      out.push_back(std::move(*q));
    if (auto q = device_query_t::parse(c.side_b))
      out.push_back(std::move(*q));
  }
  return out;
}

std::vector<device_identity_t>
ini_device_identities_from_settings(const settings_t &settings) {
  std::vector<device_identity_t> out;
  for (const auto &p : settings.ini_peers) {
    if (const auto id = device_identity_t::parse(p.identity))
      out.push_back(*id);
  }
  return out;
}

device_registry_t::device_registry_t(std::shared_ptr<midirouter_t> router,
                                     std::unique_ptr<device_db_t> db)
    : router_(std::move(router)), db_(std::move(db)) {
  if (db_ && db_->is_open()) {
    for (auto rec : db_->load_all()) {
      const auto key = rec.identity.canonical_key();
      // If multiple records canonicalize to the same key (e.g. same device
      // on different ports), merge them: keep the first-seen timestamp and
      // the higher-priority source.
      auto it = devices_.find(key);
      if (it == devices_.end()) {
        rec.identity = *device_identity_t::parse(key);
        devices_.emplace(key, std::move(rec));
      } else {
        auto &existing = it->second;
        if (static_cast<int>(rec.source) > static_cast<int>(existing.source))
          existing.source = rec.source;
        existing.first_seen =
            std::min(existing.first_seen, rec.first_seen);
        existing.last_seen =
            std::max(existing.last_seen, rec.last_seen);
      }
    }
  }
}

device_registry_t::~device_registry_t() { stop_registry_thread(); }

void device_registry_t::start_registry_thread() {
  if (registry_running_.exchange(true))
    return;
  registry_thread_ = std::thread(&device_registry_t::registry_thread_loop, this);
}

void device_registry_t::stop_registry_thread() {
  if (!registry_running_.load())
    return;
  if (!queue_.enqueue(device_registry_command_t{shutdown_t{}},
                      rtpmidid::queue_priority_e::NORMAL)) {
    registry_running_.store(false);
    queue_.wake();
  }
  if (registry_thread_.joinable())
    registry_thread_.join();
}

bool device_registry_t::on_registry_thread() const {
  return g_current_registry_thread == this;
}

bool device_registry_t::sync_mode() const { return !registry_running_.load(); }

bool device_registry_t::enqueue(device_registry_command_t &&cmd,
                                rtpmidid::queue_priority_e prio) {
  if (!queue_.enqueue(std::move(cmd), prio)) {
    ERROR("device_registry: queue full");
    return false;
  }
  return true;
}

void device_registry_t::registry_thread_loop() {
  rtpmidid::block_shutdown_signals();
  g_current_registry_thread = this;

  device_registry_command_t cmd;
  while (registry_running_.load()) {
    if (queue_.wait_dequeue(cmd))
      std::visit([this](auto &c) { handle(c); }, cmd);
  }

  while (queue_.try_dequeue(cmd))
    std::visit([this](auto &c) { handle(c); }, cmd);

  g_current_registry_thread = nullptr;
}

void device_registry_t::attach() {
  if (!router_)
    return;

  INFO("device_registry: attached to router");
  connected_connection_ = router_->connected_event.connect(
      [this](peer_id_t from, peer_id_t to) { on_connected(from, to); });
  disconnected_connection_ = router_->disconnected_event.connect(
      [this](peer_id_t from, peer_id_t to) { on_disconnected(from, to); });
  peer_added_connection_ = router_->peer_added_event.connect(
      [this](peer_id_t id) { on_peer_added(id); });
  peer_removed_connection_ = router_->peer_removed_event.connect(
      [this](peer_id_t id) { on_peer_removed(id); });
  peer_event_connection_ = router_->peer_event.connect(
      [this](peer_id_t id, midipeer_event_e evt) { on_peer_event(id, evt); });
}

void device_registry_t::seed_ini(const std::vector<device_identity_t> &devices) {
  if (sync_mode() || on_registry_thread()) {
    seed_ini_t cmd{devices};
    handle(cmd);
    return;
  }
  enqueue(device_registry_command_t{seed_ini_t{devices}},
          rtpmidid::queue_priority_e::NORMAL);
}

void device_registry_t::refresh_from_router() {
  if (sync_mode() || on_registry_thread()) {
    refresh_from_router_t cmd;
    handle(cmd);
    return;
  }
  enqueue(device_registry_command_t{refresh_from_router_t{}},
          rtpmidid::queue_priority_e::NORMAL);
}

void device_registry_t::add_manual(device_identity_t identity,
                                   std::string display_name) {
  add_manual_t cmd{std::move(identity), std::move(display_name)};
  if (sync_mode() || on_registry_thread()) {
    handle(cmd);
    return;
  }
  enqueue(device_registry_command_t{std::move(cmd)},
          rtpmidid::queue_priority_e::NORMAL);
}

bool device_registry_t::remove_device(const std::string &identity_key) {
  return dispatch_query<bool>(
      remove_device_t{identity_key, {}},
      rtpmidid::queue_priority_e::NORMAL,
      [identity_key](device_registry_t &self) {
        return self.remove_device_impl(identity_key);
      });
}

std::vector<device_record_t> device_registry_t::list_devices() const {
  return dispatch_query<std::vector<device_record_t>>(
      query_list_devices_t{{}}, rtpmidid::queue_priority_e::LOW,
      [](device_registry_t &self) { return self.list_devices_impl(); });
}

std::optional<device_record_t>
device_registry_t::find_by_identity_key(const std::string &key) const {
  return dispatch_query<std::optional<device_record_t>>(
      query_find_by_key_t{key, {}},
      rtpmidid::queue_priority_e::LOW,
      [key](device_registry_t &self) -> std::optional<device_record_t> {
        const auto it = self.devices_.find(key);
        if (it == self.devices_.end())
          return std::nullopt;
        return it->second;
      });
}

void device_registry_t::set_referenced_queries_provider(
    referenced_queries_fn provider) {
  set_referenced_provider_t cmd{std::move(provider)};
  if (sync_mode() || on_registry_thread()) {
    handle(cmd);
    return;
  }
  enqueue(device_registry_command_t{std::move(cmd)},
          rtpmidid::queue_priority_e::NORMAL);
}

size_t device_registry_t::sweep_stale_discovered(int64_t max_age_seconds) {
  return dispatch_query<size_t>(
      sweep_stale_t{max_age_seconds, {}},
      rtpmidid::queue_priority_e::NORMAL,
      [max_age_seconds](device_registry_t &self) {
        return self.sweep_stale_impl(max_age_seconds);
      });
}

size_t device_registry_t::prune_unreferenced_offline() {
  return dispatch_query<size_t>(
      prune_unreferenced_offline_t{{}},
      rtpmidid::queue_priority_e::NORMAL,
      [](device_registry_t &self) {
        return self.prune_unreferenced_offline_impl();
      });
}

void schedule_device_registry_cleanup(
    cron_tasks_t &cron, const std::shared_ptr<device_registry_t> &registry,
    std::chrono::seconds interval, int64_t max_age_seconds) {
  cron.add_periodic(
      "device_registry_sweep", interval,
      [registry, max_age_seconds]() {
        if (registry)
          registry->sweep_stale_discovered(max_age_seconds);
      });
}

void device_registry_t::notify_changed() { changed_event(); }

void device_registry_t::upsert_impl(device_record_t record, bool persist) {
  const auto key = record.identity.canonical_key();
  const auto now = now_unix();
  if (record.first_seen == 0)
    record.first_seen = now;
  if (record.last_seen == 0)
    record.last_seen = now;

  const auto it = devices_.find(key);
  if (it == devices_.end()) {
    // Store with canonical identity (ephemeral fields stripped)
    record.identity = *device_identity_t::parse(record.identity.canonical_key());
    devices_.emplace(key, std::move(record));
    if (persist && db_ && db_->is_open())
      db_->upsert(devices_.at(key));
    notify_changed();
    return;
  }

  auto &existing = it->second;
  if (static_cast<int>(record.source) > static_cast<int>(existing.source))
    existing.source = record.source;
  existing.first_seen = std::min(existing.first_seen, record.first_seen);
  existing.last_seen = std::max(existing.last_seen, record.last_seen);
  if (!record.display_name.empty())
    existing.display_name = record.display_name;
  if (record.online_peer_id)
    existing.online_peer_id = record.online_peer_id;

  if (persist && db_ && db_->is_open())
    db_->upsert(existing);
  notify_changed();
}

void device_registry_t::observe_peer_impl(peer_id_t peer_id,
                                          device_source_e source) {
  if (!router_)
    return;
  const auto row = status_row_for_peer(*router_, peer_id);
  if (!row)
    return;
  const auto identity = compute_device_identity(*row);
  if (!identity)
    return;

  device_record_t rec;
  rec.identity = *identity;
  rec.source = source;
  rec.display_name = display_name_for(*identity, row->name);
  rec.online_peer_id = peer_id;
  rec.last_seen = now_unix();
  // Map peer_id to canonical key so multiple peers (same device, different
  // ports) map to a single device record.
  peer_to_identity_[peer_id] = rec.identity.canonical_key();
  upsert_impl(std::move(rec), true);
}

void device_registry_t::mark_offline_impl(peer_id_t peer_id) {
  const auto mapped = peer_to_identity_.find(peer_id);
  if (mapped == peer_to_identity_.end())
    return;
  const std::string identity_key = mapped->second;
  const auto it = devices_.find(identity_key);
  peer_to_identity_.erase(mapped);
  if (it == devices_.end())
    return;
  it->second.online_peer_id = std::nullopt;
  it->second.last_seen = now_unix();

  // Immediately prune if this device is now offline and not referenced
  // by any saved connection.
  if (it->second.source == device_source_e::discovered) {
    std::vector<device_query_t> referenced;
    if (referenced_queries_provider_)
      referenced = referenced_queries_provider_();
    if (!is_referenced(it->second.identity, referenced)) {
      DEBUG("device_registry: pruning unreferenced offline device {}",
            identity_key);
      devices_.erase(it);
      if (db_ && db_->is_open())
        db_->remove(identity_key);
      notify_changed();
      return;
    }
  }

  if (db_ && db_->is_open())
    db_->upsert(it->second);
  notify_changed();
}

size_t device_registry_t::sweep_stale_impl(int64_t max_age_seconds) {
  std::vector<device_query_t> referenced;
  if (referenced_queries_provider_)
    referenced = referenced_queries_provider_();

  const int64_t cutoff = now_unix() - max_age_seconds;
  std::vector<device_record_t> snapshot;
  snapshot.reserve(devices_.size());
  for (const auto &kv : devices_)
    snapshot.push_back(kv.second);

  const auto stale =
      select_stale_discovered_devices(snapshot, referenced, cutoff);
  for (const auto &key : stale) {
    devices_.erase(key);
    if (db_ && db_->is_open())
      db_->remove(key);
  }
  if (!stale.empty()) {
    INFO("device_registry: pruned {} stale discovered device(s)", stale.size());
    notify_changed();
  }
  return stale.size();
}

size_t device_registry_t::prune_unreferenced_offline_impl() {
  std::vector<device_query_t> referenced;
  if (referenced_queries_provider_)
    referenced = referenced_queries_provider_();

  std::vector<std::string> to_remove;
  for (const auto &kv : devices_) {
    const auto &rec = kv.second;
    if (rec.source != device_source_e::discovered)
      continue;
    if (rec.online())
      continue;
    if (is_referenced(rec.identity, referenced))
      continue;
    to_remove.push_back(kv.first);
  }

  for (const auto &key : to_remove) {
    devices_.erase(key);
    if (db_ && db_->is_open())
      db_->remove(key);
  }

  if (!to_remove.empty()) {
    INFO("device_registry: pruned {} unreferenced offline device(s)",
         to_remove.size());
    notify_changed();
  }
  return to_remove.size();
}

bool device_registry_t::remove_device_impl(const std::string &identity_key) {
  const auto it = devices_.find(identity_key);
  if (it == devices_.end())
    return false;
  devices_.erase(it);
  if (db_ && db_->is_open())
    db_->remove(identity_key);
  notify_changed();
  return true;
}

std::vector<device_record_t> device_registry_t::list_devices_impl() {
  std::vector<device_record_t> out;
  out.reserve(devices_.size());
  for (const auto &kv : devices_)
    out.push_back(kv.second);
  std::sort(out.begin(), out.end(), [](const device_record_t &a,
                                       const device_record_t &b) {
    return a.identity_key() < b.identity_key();
  });
  return out;
}

void device_registry_t::handle(seed_ini_t &cmd) {
  for (const auto &identity : cmd.devices) {
    device_record_t rec;
    rec.identity = identity;
    rec.source = device_source_e::ini;
    rec.display_name = display_name_for(identity, std::nullopt);
    upsert_impl(std::move(rec), true);
  }
}

void device_registry_t::handle(observe_peer_t &cmd) {
  observe_peer_impl(cmd.peer_id, cmd.source);
}

void device_registry_t::handle(mark_offline_t &cmd) {
  mark_offline_impl(cmd.peer_id);
}

void device_registry_t::handle(add_manual_t &cmd) {
  device_record_t rec;
  rec.identity = std::move(cmd.identity);
  rec.source = device_source_e::manual;
  rec.display_name =
      cmd.display_name.empty()
          ? display_name_for(rec.identity, std::nullopt)
          : std::move(cmd.display_name);
  upsert_impl(std::move(rec), true);
}

void device_registry_t::handle(remove_device_t &cmd) {
  post_reply(cmd.reply, remove_device_impl(cmd.identity_key));
}

void device_registry_t::handle(refresh_from_router_t &) {
  if (!router_)
    return;

  peer_to_identity_.clear();
  for (const auto &row : router_->status_rows()) {
    if (!row.id)
      continue;
    const auto identity = compute_device_identity(row);
    if (!identity)
      continue;
    const peer_id_t pid = static_cast<peer_id_t>(*row.id);
    device_record_t rec;
    rec.identity = *identity;
    rec.source = device_source_e::discovered;
    rec.display_name = display_name_for(*identity, row.name);
    rec.online_peer_id = pid;
    rec.last_seen = now_unix();
    peer_to_identity_[pid] = rec.identity.canonical_key();
    upsert_impl(std::move(rec), true);
  }
}

void device_registry_t::handle(sweep_stale_t &cmd) {
  const size_t pruned = sweep_stale_impl(cmd.max_age_seconds);
  post_reply(cmd.reply, pruned);
}

void device_registry_t::handle(prune_unreferenced_offline_t &cmd) {
  const size_t pruned = prune_unreferenced_offline_impl();
  post_reply(cmd.reply, pruned);
}

void device_registry_t::handle(set_referenced_provider_t &cmd) {
  referenced_queries_provider_ = std::move(cmd.provider);
}

void device_registry_t::handle(query_list_devices_t &cmd) {
  post_reply(cmd.reply, list_devices_impl());
}

void device_registry_t::handle(query_find_by_key_t &cmd) {
  std::optional<device_record_t> out;
  const auto it = devices_.find(cmd.identity_key);
  if (it != devices_.end())
    out = it->second;
  post_reply(cmd.reply, std::move(out));
}

void device_registry_t::handle(shutdown_t &) {
  registry_running_.store(false);
}

void device_registry_t::on_connected(peer_id_t /*from*/, peer_id_t /*to*/) {}

void device_registry_t::on_disconnected(peer_id_t /*from*/, peer_id_t /*to*/) {}

void device_registry_t::on_peer_added(peer_id_t peer_id) {
  if (sync_mode() || on_registry_thread()) {
    observe_peer_impl(peer_id, device_source_e::discovered);
    return;
  }
  enqueue(device_registry_command_t{observe_peer_t{
              peer_id, device_source_e::discovered}},
          rtpmidid::queue_priority_e::NORMAL);
}

void device_registry_t::on_peer_removed(peer_id_t peer_id) {
  if (sync_mode() || on_registry_thread()) {
    mark_offline_impl(peer_id);
    return;
  }
  enqueue(device_registry_command_t{mark_offline_t{peer_id}},
          rtpmidid::queue_priority_e::NORMAL);
}

void device_registry_t::on_peer_event(peer_id_t peer_id, midipeer_event_e evt) {
  if (evt == midipeer_event_e::CONNECTED_PEER)
    on_peer_added(peer_id);
}

} // namespace rtpmididns

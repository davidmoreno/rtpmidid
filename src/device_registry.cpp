/**
 * Phase 4: device registry implementation.
 */
#include "device_registry.hpp"

#include "device_db.hpp"
#include "device_identity_from_peer.hpp"
#include "settings.hpp"

#include "rtpmidid/logger.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace rtpmididns {

namespace {

int64_t now_unix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string display_name_for(const device_identity_t &identity,
                             const std::optional<std::string> &row_name) {
  if (row_name && !row_name->empty())
    return *row_name;
  for (const auto &f : identity.fields) {
    if (f.key == "name" || f.key == "client" || f.key == "service")
      return f.value;
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

} // namespace

std::vector<device_identity_t>
ini_device_identities_from_settings(const settings_t &settings) {
  std::vector<device_identity_t> out;

  for (const auto &announce : settings.alsa_announces) {
    if (announce.name.empty())
      continue;
    out.push_back(device_identity_t{
        "alsa_multi", {device_identity_field_t{"name", announce.name, false}}});
  }

  for (const auto &announce : settings.rtpmidi_announces) {
    if (announce.name.empty())
      continue;
    std::vector<device_identity_field_t> fields{
        {"name", announce.name, false}};
    if (!announce.port.empty())
      fields.push_back({"port", announce.port, false});
    out.push_back(device_identity_t{"rtpmidi_multi", std::move(fields)});
  }

  for (const auto &raw : settings.rawmidi) {
    std::vector<device_identity_field_t> fields;
    if (!raw.device.empty())
      fields.push_back({"device", raw.device, false});
    if (!raw.name.empty())
      fields.push_back({"name", raw.name, false});
    if (!fields.empty())
      out.push_back(device_identity_t{"rawmidi", std::move(fields)});
  }

  for (const auto &connect : settings.connect_to) {
    if (connect.name.empty())
      continue;
    out.push_back(device_identity_t{
        "alsa_listener", {device_identity_field_t{"name", connect.name, false}}});
  }

  return out;
}

device_registry_t::device_registry_t(std::shared_ptr<midirouter_t> router,
                                     std::unique_ptr<device_db_t> db)
    : router_(std::move(router)), db_(std::move(db)) {
  if (db_ && db_->is_open()) {
    for (auto rec : db_->load_all()) {
      const auto key = rec.identity_key();
      devices_.emplace(key, std::move(rec));
    }
  }
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

int device_registry_t::source_priority(device_source_e source) {
  switch (source) {
  case device_source_e::manual:
    return 3;
  case device_source_e::ini:
    return 2;
  case device_source_e::discovered:
    return 1;
  }
  return 0;
}

void device_registry_t::upsert_locked(device_record_t record, bool persist) {
  const auto key = record.identity_key();
  const auto now = now_unix();
  if (record.first_seen == 0)
    record.first_seen = now;
  if (record.last_seen == 0)
    record.last_seen = now;

  const auto it = devices_.find(key);
  if (it == devices_.end()) {
    devices_.emplace(key, std::move(record));
    if (persist && db_ && db_->is_open())
      db_->upsert(devices_.at(key));
    notify_changed();
    return;
  }

  auto &existing = it->second;
  if (source_priority(record.source) > source_priority(existing.source))
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

void device_registry_t::notify_changed() { changed_event(); }

void device_registry_t::seed_ini(const std::vector<device_identity_t> &devices) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &identity : devices) {
    device_record_t rec;
    rec.identity = identity;
    rec.source = device_source_e::ini;
    rec.display_name = display_name_for(identity, std::nullopt);
    upsert_locked(std::move(rec), true);
  }
}

void device_registry_t::refresh_from_router() {
  if (!router_)
    return;

  const auto rows = router_->status_rows();
  std::lock_guard<std::mutex> lock(mutex_);
  peer_to_identity_.clear();
  for (const auto &row : rows) {
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
    peer_to_identity_[pid] = rec.identity_key();
    upsert_locked(std::move(rec), true);
  }
}

void device_registry_t::add_manual(device_identity_t identity,
                                   std::string display_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  device_record_t rec;
  rec.identity = std::move(identity);
  rec.source = device_source_e::manual;
  rec.display_name =
      display_name.empty()
          ? display_name_for(rec.identity, std::nullopt)
          : std::move(display_name);
  upsert_locked(std::move(rec), true);
}

bool device_registry_t::remove_manual(const std::string &identity_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = devices_.find(identity_key);
  if (it == devices_.end() || it->second.source != device_source_e::manual)
    return false;
  devices_.erase(it);
  if (db_ && db_->is_open())
    db_->remove(identity_key);
  notify_changed();
  return true;
}

std::vector<device_record_t> device_registry_t::list_devices() const {
  std::lock_guard<std::mutex> lock(mutex_);
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

std::optional<device_record_t>
device_registry_t::find_by_identity_key(const std::string &key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = devices_.find(key);
  if (it == devices_.end())
    return std::nullopt;
  return it->second;
}

void device_registry_t::observe_peer(peer_id_t peer_id,
                                     device_source_e source) {
  if (!router_)
    return;
  const auto row = status_row_for_peer(*router_, peer_id);
  if (!row)
    return;
  const auto identity = compute_device_identity(*row);
  if (!identity)
    return;

  std::lock_guard<std::mutex> lock(mutex_);
  device_record_t rec;
  rec.identity = *identity;
  rec.source = source;
  rec.display_name = display_name_for(*identity, row->name);
  rec.online_peer_id = peer_id;
  rec.last_seen = now_unix();
  peer_to_identity_[peer_id] = rec.identity_key();
  upsert_locked(std::move(rec), true);
}

void device_registry_t::mark_offline_locked(peer_id_t peer_id) {
  const auto mapped = peer_to_identity_.find(peer_id);
  if (mapped == peer_to_identity_.end())
    return;
  const auto it = devices_.find(mapped->second);
  peer_to_identity_.erase(mapped);
  if (it == devices_.end())
    return;
  it->second.online_peer_id = std::nullopt;
  it->second.last_seen = now_unix();
  if (db_ && db_->is_open())
    db_->upsert(it->second);
  notify_changed();
}

void device_registry_t::on_connected(peer_id_t /*from*/, peer_id_t /*to*/) {}

void device_registry_t::on_disconnected(peer_id_t /*from*/, peer_id_t /*to*/) {}

void device_registry_t::on_peer_added(peer_id_t peer_id) {
  observe_peer(peer_id, device_source_e::discovered);
}

void device_registry_t::on_peer_removed(peer_id_t peer_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  mark_offline_locked(peer_id);
}

void device_registry_t::on_peer_event(peer_id_t peer_id, midipeer_event_e evt) {
  if (evt == midipeer_event_e::CONNECTED_PEER)
    observe_peer(peer_id, device_source_e::discovered);
}

} // namespace rtpmididns

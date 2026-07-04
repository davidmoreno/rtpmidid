/**
 * Phase 5: connection_db v2 + directed restore manager.
 */
#include "connection_db.hpp"
#include "connection_alsa_direct.hpp"
#include "connection_restore.hpp"
#include "device_identity_from_peer.hpp"
#include "aseq.hpp"
#include "rtpmidid/logger.hpp"
#include <algorithm>
#include <sqlite3.h>
#include <utility>

namespace rtpmididns {

namespace {

connection_direction_e merge_direction(connection_direction_e existing,
                                       connection_direction_e incoming) {
  if (existing == incoming)
    return existing;
  if (existing == connection_direction_e::both ||
      incoming == connection_direction_e::both)
    return connection_direction_e::both;
  return connection_direction_e::both;
}

} // namespace

connection_direction_e connection_direction_from_wire(std::string_view wire) {
  if (wire == "a2b")
    return connection_direction_e::a2b;
  if (wire == "b2a")
    return connection_direction_e::b2a;
  return connection_direction_e::both;
}

const char *connection_direction_to_wire(connection_direction_e direction) {
  switch (direction) {
  case connection_direction_e::a2b:
    return "a2b";
  case connection_direction_e::b2a:
    return "b2a";
  case connection_direction_e::both:
    return "both";
  }
  return "both";
}

connection_direction_e flip_connection_direction(connection_direction_e direction) {
  switch (direction) {
  case connection_direction_e::a2b:
    return connection_direction_e::b2a;
  case connection_direction_e::b2a:
    return connection_direction_e::a2b;
  case connection_direction_e::both:
    return connection_direction_e::both;
  }
  return connection_direction_e::both;
}

stored_connection_t canonicalize_stored_connection(stored_connection_t connection) {
  if (connection.side_b < connection.side_a) {
    std::swap(connection.side_a, connection.side_b);
    connection.direction = flip_connection_direction(connection.direction);
  }
  return connection;
}

std::pair<std::string, std::string>
connection_db_t::normalize_sides(std::string a, std::string b) {
  if (b < a)
    std::swap(a, b);
  return {std::move(a), std::move(b)};
}

void connection_db_t::migrate_schema() {
  if (!db_.is_open())
    return;

  const char *create =
      "CREATE TABLE IF NOT EXISTS connections ("
      "  side_a TEXT NOT NULL,"
      "  side_b TEXT NOT NULL,"
      "  direction TEXT NOT NULL DEFAULT 'both',"
      "  enabled INTEGER NOT NULL DEFAULT 1,"
      "  PRIMARY KEY (side_a, side_b)"
      ");";

  if (!db_.exec(create, "connection_db schema init")) {
    db_.close();
    return;
  }

  const char *alters[] = {
      "ALTER TABLE connections ADD COLUMN direction TEXT NOT NULL DEFAULT 'both';",
      "ALTER TABLE connections ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1;",
  };
  const char *columns[] = {"direction", "enabled"};
  for (size_t i = 0; i < sizeof(columns) / sizeof(columns[0]); ++i) {
    if (!db_.has_table_column("connections", columns[i]))
      db_.exec(alters[i], "connection_db schema migrate");
  }
}

connection_db_t::connection_db_t(std::string path)
    : path_(path), db_(std::move(path)) {
  if (!db_.is_open())
    return;
  migrate_schema();
  if (!db_.is_open())
    return;
  INFO("component=database connection_db: opened {}", path_);
}

void connection_db_t::save_connection(const stored_connection_t &connection) {
  if (!db_.is_open() || connection.side_a.empty() || connection.side_b.empty())
    return;

  stored_connection_t merged = connection;
  {
    auto lock = db_.lock();
    auto find = db_.prepare(
        "SELECT direction, enabled FROM connections WHERE side_a = ? AND side_b = ?;",
        "connection_db save find");
    if (find) {
      find->bind_text(1, connection.side_a);
      find->bind_text(2, connection.side_b);
      if (find->step() == SQLITE_ROW) {
        const auto dir = find->column_text(0);
        merged.direction =
            merge_direction(connection_direction_from_wire(dir ? *dir : "both"),
                            connection.direction);
        merged.enabled =
            find->column_int(1) != 0 && connection.enabled;
      }
    }
  }

  auto lock = db_.lock();
  auto stmt = db_.prepare(
      "INSERT OR REPLACE INTO connections (side_a, side_b, direction, enabled) "
      "VALUES (?, ?, ?, ?);",
      "connection_db save");
  if (!stmt)
    return;

  stmt->bind_text(1, merged.side_a);
  stmt->bind_text(2, merged.side_b);
  stmt->bind_text(3, connection_direction_to_wire(merged.direction));
  stmt->bind_int(4, merged.enabled ? 1 : 0);

  if (stmt->step() != SQLITE_DONE) {
    ERROR("component=database connection_db: save failed: {}",
          sqlite3_errmsg(db_.raw()));
  } else {
    INFO("component=database connection_db: stored {} {} -> {} (enabled={})",
         connection_direction_to_wire(merged.direction), merged.side_a,
         merged.side_b, merged.enabled ? 1 : 0);
  }
}

void connection_db_t::remove_connection(const std::string &side_a,
                                        const std::string &side_b) {
  if (!db_.is_open())
    return;

  // Normalise order to match canonicalized storage.
  auto a = side_a;
  auto b = side_b;
  if (b < a)
    std::swap(a, b);

  auto lock = db_.lock();
  auto stmt = db_.prepare(
      "DELETE FROM connections WHERE side_a = ? AND side_b = ?;",
      "connection_db delete");
  if (!stmt)
    return;

  stmt->bind_text(1, a);
  stmt->bind_text(2, b);

  if (stmt->step() != SQLITE_DONE) {
    ERROR("component=database connection_db: delete failed: {}",
          sqlite3_errmsg(db_.raw()));
  } else if (db_.changes() > 0) {
    INFO("component=database connection_db: deleted {} -> {}", a, b);
  } else {
    // Legacy: try the reverse order for rows saved before canonicalization
    auto stmt2 = db_.prepare(
        "DELETE FROM connections WHERE side_a = ? AND side_b = ?;",
        "connection_db delete legacy");
    if (stmt2) {
      stmt2->bind_text(1, b);
      stmt2->bind_text(2, a);
      if (stmt2->step() == SQLITE_DONE && db_.changes() > 0) {
        INFO("component=database connection_db: deleted (legacy order) {} -> {}", b, a);
      }
    }
  }
}

bool connection_db_t::set_enabled(const std::string &side_a,
                                  const std::string &side_b, bool enabled) {
  if (!db_.is_open())
    return false;

  auto lock = db_.lock();
  auto stmt = db_.prepare(
      "UPDATE connections SET enabled = ? WHERE side_a = ? AND side_b = ?;",
      "connection_db enable");
  if (!stmt)
    return false;

  stmt->bind_int(1, enabled ? 1 : 0);
  stmt->bind_text(2, side_a);
  stmt->bind_text(3, side_b);

  const bool ok = stmt->step() == SQLITE_DONE && db_.changes() > 0;
  return ok;
}

std::vector<stored_connection_t> connection_db_t::list_connections() const {
  std::vector<stored_connection_t> out;
  if (!db_.is_open())
    return out;

  auto lock = db_.lock();
  auto stmt = db_.prepare(
      "SELECT side_a, side_b, direction, enabled FROM connections;",
      "connection_db list");
  if (!stmt)
    return out;

  while (stmt->step() == SQLITE_ROW) {
    stored_connection_t row;
    if (const auto a = stmt->column_text(0))
      row.side_a = std::string{*a};
    if (const auto b = stmt->column_text(1))
      row.side_b = std::string{*b};
    if (const auto dir = stmt->column_text(2))
      row.direction = connection_direction_from_wire(*dir);
    row.enabled = stmt->column_int(3) != 0;
    if (!row.side_a.empty() && !row.side_b.empty())
      out.push_back(std::move(row));
  }
  return out;
}

void connection_db_t::record_connection(const std::string &side_a,
                                          const std::string &side_b) {
  const auto sides = normalize_sides(side_a, side_b);
  stored_connection_t row;
  row.side_a = sides.first;
  row.side_b = sides.second;
  row.direction = connection_direction_e::both;
  row.enabled = true;
  save_connection(row);
}

std::vector<connection_pair_t> connection_db_t::get_connections() const {
  std::vector<connection_pair_t> out;
  for (const auto &row : list_connections()) {
    out.push_back(connection_pair_t{row.side_a, row.side_b});
  }
  return out;
}

connection_db_manager_t::connection_db_manager_t(
    std::shared_ptr<midirouter_t> router, std::unique_ptr<connection_db_t> db)
    : router_(std::move(router)), db_(std::move(db)) {}

void connection_db_manager_t::attach() {
  if (!db_ || !db_->is_open() || !router_)
    return;

  INFO("component=database connection_db: attached to router (persist + auto-reconnect enabled)");
  connected_connection_ = router_->connected_event.connect(
      [this](peer_id_t from, peer_id_t to) { on_connected(from, to); });
  disconnected_connection_ = router_->disconnected_event.connect(
      [this](peer_id_t from, peer_id_t to) { on_disconnected(from, to); });
  peer_added_connection_ = router_->peer_added_event.connect(
      [this](peer_id_t id) { on_peer_added(id); });
  peer_event_connection_ = router_->peer_event.connect(
      [this](peer_id_t id, midipeer_event_e evt) { on_peer_event(id, evt); });
}

void connection_db_manager_t::attach_aseq(std::shared_ptr<aseq_t> aseq) {
  if (!db_ || !db_->is_open())
    return;
  if (!aseq) {
    aseq_.reset();
    return;
  }
  aseq_ = std::move(aseq);
  INFO("component=database connection_db: attached to ALSA sequencer (pure-ALSA pair "
       "auto-aconnect enabled)");
  aseq_port_added_connection_ = aseq_->added_port_announcement.connect(
      [this](const std::string & /*name*/, aseq_t::client_type_e /*type*/,
             const aseq_t::port_t & /*port*/) {
        try_auto_aconnect_all_alsa_pairs();
      });
}

std::vector<online_device_t>
connection_db_manager_t::collect_online_devices() const {
  std::vector<online_device_t> out;
  if (!router_)
    return out;

  for (const auto &row : router_->status_rows()) {
    if (!row.id)
      continue;
    const auto identity = compute_device_identity(row);
    if (!identity)
      continue;
    online_device_t device;
    device.peer_id = static_cast<peer_id_t>(*row.id);
    device.identity = *identity;
    out.push_back(std::move(device));
  }
  return out;
}

std::optional<device_identity_t>
connection_db_manager_t::device_identity_for_peer(peer_id_t peer_id) const {
  if (!router_)
    return std::nullopt;
  for (const auto &row : router_->status_rows()) {
    if (row.id && static_cast<peer_id_t>(*row.id) == peer_id)
      return compute_device_identity(row);
  }
  return std::nullopt;
}

void connection_db_manager_t::apply_saved_connections() {
  if (!db_ || !db_->is_open() || !router_)
    return;

  const auto online = collect_online_devices();
  const auto saved = db_->list_connections();
  const auto actions = plan_connection_restore(
      saved, online, [this](peer_id_t from, peer_id_t to) {
        const auto targets = router_->send_targets_for(from);
        return std::find(targets.begin(), targets.end(), to) != targets.end();
      });

  for (const auto &action : actions) {
    INFO("component=database connection_db: restoring directed edge peer {} -> {}", action.from,
         action.to);
    router_->enqueue_connect(action.from, action.to);
  }
}

void connection_db_manager_t::try_record_pair(peer_id_t from, peer_id_t to) {
  if (!db_ || !db_->is_open())
    return;

  const auto id_from = device_identity_for_peer(from);
  const auto id_to = device_identity_for_peer(to);

  if (id_from && id_to) {
    stored_connection_t row;
    row.side_a = id_from->serialize();
    row.side_b = id_to->serialize();
    row.direction = connection_direction_e::a2b;
    row.enabled = true;
    row = canonicalize_stored_connection(std::move(row));
    INFO("component=database connection_db: storing live router edge {} -> {}", row.side_a,
         row.side_b);
    db_->save_connection(row);
    std::lock_guard<std::mutex> lock(state_mutex_);
    pending_records_.erase(
        std::remove_if(pending_records_.begin(), pending_records_.end(),
                       [from, to](const pending_pair_t &p) {
                         return (p.a == from && p.b == to) ||
                                (p.a == to && p.b == from);
                       }),
        pending_records_.end());
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto &p : pending_records_) {
      if ((p.a == from && p.b == to) || (p.a == to && p.b == from))
        return;
    }
    pending_records_.push_back(pending_pair_t{from, to});
  }
}

void connection_db_manager_t::try_finalize_pending_for(peer_id_t peer_id) {
  std::vector<pending_pair_t> snapshot;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    snapshot = pending_records_;
  }
  for (const auto &p : snapshot) {
    if (p.a == peer_id || p.b == peer_id)
      try_record_pair(p.a, p.b);
  }
}

void connection_db_manager_t::on_connected(peer_id_t from, peer_id_t to) {
  DEBUG("connection_db: router connected peer {} -> {}", from, to);
  try_record_pair(from, to);
}

void connection_db_manager_t::on_disconnected(peer_id_t from, peer_id_t to) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    pending_records_.erase(
        std::remove_if(pending_records_.begin(), pending_records_.end(),
                       [from, to](const pending_pair_t &p) {
                         return (p.a == from && p.b == to) ||
                                (p.a == to && p.b == from);
                       }),
        pending_records_.end());
  }

  if (!db_ || !db_->is_open())
    return;

  if (const auto id_from = device_identity_for_peer(from)) {
    if (const auto id_to = device_identity_for_peer(to)) {
      INFO("component=database connection_db: removing live router edge {} -> {}",
           id_from->serialize(), id_to->serialize());
      db_->remove_connection(id_from->serialize(), id_to->serialize());
    }
  }
}

void connection_db_manager_t::on_peer_added(peer_id_t /*peer_id*/) {
  apply_saved_connections();
}

void connection_db_manager_t::on_peer_event(peer_id_t peer_id,
                                           midipeer_event_e evt) {
  if (evt == midipeer_event_e::CONNECTED_PEER) {
    try_finalize_pending_for(peer_id);
    apply_saved_connections();
  } else if (evt == midipeer_event_e::DISCONNECTED_PEER) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    pending_records_.erase(
        std::remove_if(pending_records_.begin(), pending_records_.end(),
                       [peer_id](const pending_pair_t &p) {
                         return p.a == peer_id || p.b == peer_id;
                       }),
        pending_records_.end());
  }
}

void connection_db_manager_t::check_reconnects_for_all() {
  if (!router_)
    return;
  const auto saved =
      db_ && db_->is_open() ? db_->list_connections()
                            : std::vector<stored_connection_t>{};
  INFO("component=database connection_db: startup reconnect scan ({} peer(s), {} saved "
       "connection(s))",
       router_->peer_ids().size(), saved.size());
  apply_saved_connections();
  try_auto_aconnect_all_alsa_pairs();
}

void connection_db_manager_t::try_auto_aconnect_all_alsa_pairs() {
  if (!db_ || !db_->is_open() || !aseq_)
    return;

  const auto saved = db_->list_connections();
  const auto ports = aseq_->enumerate_exported_ports();
  const auto actions = plan_alsa_aconnect_actions(saved, ports);

  for (const auto &action : actions) {
    bool already = false;
    aseq_->for_connections(action.from, [&already, &action](const aseq_t::port_t &other) {
      if (other == action.to)
        already = true;
    });
    if (already)
      continue;
    try {
      aseq_->connect_external(action.from, action.to);
      INFO("component=database connection_db: aconnect {}:{} -> {}:{}", action.from.client,
           action.from.port, action.to.client, action.to.port);
    } catch (const std::exception &e) {
      ERROR("component=database connection_db: aconnect failed: {}", e.what());
    }
  }
}

void connection_db_manager_t::save_stored_connection(
    stored_connection_t connection) {
  if (!db_ || !db_->is_open())
    return;
  connection = canonicalize_stored_connection(std::move(connection));
  db_->save_connection(connection);
  check_reconnects_for_all();
}

bool connection_db_manager_t::set_stored_enabled(const std::string &side_a,
                                                 const std::string &side_b,
                                                 bool enabled) {
  if (!db_ || !db_->is_open())
    return false;
  stored_connection_t probe;
  probe.side_a = side_a;
  probe.side_b = side_b;
  probe = canonicalize_stored_connection(std::move(probe));
  const bool ok =
      db_->set_enabled(probe.side_a, probe.side_b, enabled);
  if (ok && enabled)
    check_reconnects_for_all();
  return ok;
}

void connection_db_manager_t::record_stable_pair(const std::string &side_a,
                                                 const std::string &side_b) {
  if (!db_ || !db_->is_open())
    return;
  INFO("component=database connection_db: store requested {} <-> {}", side_a, side_b);
  db_->record_connection(side_a, side_b);
  check_reconnects_for_all();
}

void connection_db_manager_t::remove_stable_pair(const std::string &side_a,
                                                 const std::string &side_b) {
  if (!db_ || !db_->is_open())
    return;
  INFO("component=database connection_db: delete requested {} <-> {}", side_a, side_b);
  auto a = side_a;
  auto b = side_b;
  if (b < a)
    std::swap(a, b);
  db_->remove_connection(a, b);
}

} // namespace rtpmididns

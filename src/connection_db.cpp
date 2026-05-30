/**
 * Phase 5: connection_db v2 + directed restore manager.
 */
#include "connection_db.hpp"
#include "connection_restore.hpp"
#include "device_identity_from_peer.hpp"
#include "aseq.hpp"
#include "peer_device_alsa_seq.hpp"
#include "peer_device_rawmidi.hpp"
#include "peer_device_rtpmidi_client.hpp"
#include "peer_device_rtpmidi_session.hpp"
#include "peer_export_alsa_network.hpp"
#include "peer_export_rtpmidi_server.hpp"
#include "peer_import_alsa_rtp.hpp"
#include "peer_import_rtpmidi.hpp"
#include "peer_kind.hpp"
#include "peer_stable_id.hpp"
#include "rtpmidid/logger.hpp"
#include <algorithm>
#include <sqlite3.h>
#include <utility>

namespace rtpmididns {

namespace {

std::string unescape_stable_component(std::string s) {
  for (char &c : s) {
    if (c == '|')
      c = ':';
  }
  return s;
}

std::optional<std::pair<std::string, std::string>>
parse_alsa_stable_id(const std::string &stable_id) {
  static const std::string kPrefix = "alsa:";
  if (stable_id.size() <= kPrefix.size())
    return std::nullopt;
  if (stable_id.compare(0, kPrefix.size(), kPrefix) != 0)
    return std::nullopt;
  const std::string rest = stable_id.substr(kPrefix.size());
  const auto pos = rest.find(':');
  if (pos == std::string::npos || pos == 0 || pos + 1 == rest.size())
    return std::nullopt;
  return std::make_pair(unescape_stable_component(rest.substr(0, pos)),
                        unescape_stable_component(rest.substr(pos + 1)));
}

std::optional<std::pair<std::string, std::string>>
parse_alsa_seq_identity(const std::string &identity_key) {
  const auto id = device_identity_t::parse(identity_key);
  if (!id || id->type_prefix != "alsa_seq")
    return std::nullopt;
  std::optional<std::string> client;
  std::optional<std::string> port;
  for (const auto &f : id->fields) {
    if (f.key == "client")
      client = f.value;
    else if (f.key == "port")
      port = f.value;
  }
  if (!client || !port)
    return std::nullopt;
  return std::make_pair(*client, *port);
}

bool is_direct_alsa_side(const std::string &side) {
  return parse_alsa_stable_id(side).has_value() ||
         parse_alsa_seq_identity(side).has_value();
}

std::optional<std::pair<std::string, std::string>>
alsa_side_names(const std::string &side) {
  if (auto legacy = parse_alsa_stable_id(side))
    return legacy;
  return parse_alsa_seq_identity(side);
}

std::optional<aseq_t::port_t>
find_alsa_port_by_names(const std::vector<alsa_seq_port_row_t> &ports,
                        const std::string &client_name,
                        const std::string &port_name) {
  for (const auto &p : ports) {
    if (p.client_name == client_name && p.port_name == port_name) {
      return aseq_t::port_t{static_cast<uint8_t>(p.client),
                            static_cast<uint8_t>(p.port)};
    }
  }
  return std::nullopt;
}

bool alsa_is_already_connected(aseq_t &aseq, const aseq_t::port_t &from,
                               const aseq_t::port_t &to) {
  bool found = false;
  aseq.for_connections(from, [&found, &to](const aseq_t::port_t &other) {
    if (other == to)
      found = true;
  });
  return found;
}

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

std::optional<std::string> compute_stable_id(const router_peer_row_t &row) {
  const auto kind = peer_kind_from_wire_type(row.type.value_or(""));
  if (kind) {
    switch (*kind) {
    case peer_kind_e::device_alsa_seq:
      return peer_device_alsa_seq_t::stable_id_from_row(row);
    case peer_kind_e::device_rawmidi:
      return peer_device_rawmidi_t::stable_id_from_row(row);
    case peer_kind_e::device_rtpmidi_client:
      return peer_device_rtpmidi_client_t::stable_id_from_row(row);
    case peer_kind_e::device_rtpmidi_session:
      return peer_device_rtpmidi_session_t::stable_id_from_row(row);
    case peer_kind_e::export_rtpmidi_server:
      return peer_export_rtpmidi_server_t::stable_id_from_row(row);
    case peer_kind_e::import_alsa_rtp:
      return peer_import_alsa_rtp_t::stable_id_from_row(row);
    case peer_kind_e::import_rtpmidi:
      return peer_import_rtpmidi_t::stable_id_from_row(row);
    case peer_kind_e::export_alsa_network:
      return peer_export_alsa_network_t::stable_id_from_row(row);
    case peer_kind_e::webui_monitor:
      return std::nullopt;
    case peer_kind_e::unknown:
      break;
    }
  }

  const std::string type = row.type.value_or("");
  const std::string peer_name =
      row.name && !row.name->empty() ? *row.name : std::string();
  if (!type.empty() && !peer_name.empty()) {
    std::string prefix = type;
    if (prefix.size() > 2 && prefix.compare(prefix.size() - 2, 2, "_t") == 0)
      prefix.resize(prefix.size() - 2);
    return make_stable_id(prefix, {peer_name});
  }
  return std::nullopt;
}

std::optional<peer_id_t>
find_peer_id_for_stable_id(const std::vector<router_peer_row_t> &rows,
                           const std::string &stable_id) {
  for (const auto &row : rows) {
    const auto sid = compute_stable_id(row);
    if (sid && *sid == stable_id && row.id)
      return static_cast<peer_id_t>(*row.id);
    const auto did = compute_device_identity(row);
    if (did && did->serialize() == stable_id && row.id)
      return static_cast<peer_id_t>(*row.id);
  }
  return std::nullopt;
}

std::pair<std::string, std::string>
connection_db_t::normalize_sides(std::string a, std::string b) {
  if (b < a)
    std::swap(a, b);
  return {std::move(a), std::move(b)};
}

void connection_db_t::migrate_schema() {
  if (!db_)
    return;

  const char *create =
      "CREATE TABLE IF NOT EXISTS connections ("
      "  side_a TEXT NOT NULL,"
      "  side_b TEXT NOT NULL,"
      "  direction TEXT NOT NULL DEFAULT 'both',"
      "  enabled INTEGER NOT NULL DEFAULT 1,"
      "  PRIMARY KEY (side_a, side_b)"
      ");";

  char *errmsg = nullptr;
  if (sqlite3_exec(db_.get(), create, nullptr, nullptr, &errmsg) != SQLITE_OK) {
    ERROR("connection_db: schema init failed: {}",
          errmsg ? errmsg : "unknown error");
    sqlite3_free(errmsg);
    db_.reset();
    return;
  }

  const char *alters[] = {
      "ALTER TABLE connections ADD COLUMN direction TEXT NOT NULL DEFAULT 'both';",
      "ALTER TABLE connections ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1;",
  };
  for (const char *sql : alters) {
    if (sqlite3_exec(db_.get(), sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
      sqlite3_free(errmsg);
      errmsg = nullptr;
    }
  }
}

connection_db_t::connection_db_t(std::string path) {
  if (path.empty())
    return;

  sqlite3 *raw = nullptr;
  const int rc = sqlite3_open(path.c_str(), &raw);
  if (rc != SQLITE_OK) {
    ERROR("connection_db: cannot open {}: {}", path,
          raw ? sqlite3_errmsg(raw) : "unknown error");
    sqlite3_deleter{}(raw);
    return;
  }
  db_.reset(raw);
  migrate_schema();
  if (!db_)
    return;

  INFO("connection_db: opened {}", path);
}

void connection_db_t::save_connection(const stored_connection_t &connection) {
  if (!db_ || connection.side_a.empty() || connection.side_b.empty())
    return;

  stored_connection_t merged = connection;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt *find = nullptr;
    const char *find_sql =
        "SELECT direction, enabled FROM connections WHERE side_a = ? AND side_b = ?;";
    if (sqlite3_prepare_v2(db_.get(), find_sql, -1, &find, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(find, 1, connection.side_a.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(find, 2, connection.side_b.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(find) == SQLITE_ROW) {
        const char *dir =
            reinterpret_cast<const char *>(sqlite3_column_text(find, 0));
        merged.direction =
            merge_direction(connection_direction_from_wire(dir ? dir : "both"),
                            connection.direction);
        merged.enabled =
            sqlite3_column_int(find, 1) != 0 && connection.enabled;
      }
      sqlite3_finalize(find);
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT OR REPLACE INTO connections (side_a, side_b, direction, enabled) "
      "VALUES (?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("connection_db: prepare save failed: {}", sqlite3_errmsg(db_.get()));
    return;
  }

  sqlite3_bind_text(stmt, 1, merged.side_a.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, merged.side_b.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, connection_direction_to_wire(merged.direction), -1,
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 4, merged.enabled ? 1 : 0);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    ERROR("connection_db: save failed: {}", sqlite3_errmsg(db_.get()));
  } else {
    INFO("connection_db: stored {} {} -> {} (enabled={})",
         connection_direction_to_wire(merged.direction), merged.side_a,
         merged.side_b, merged.enabled ? 1 : 0);
  }
  sqlite3_finalize(stmt);
}

void connection_db_t::remove_connection(const std::string &side_a,
                                        const std::string &side_b) {
  if (!db_)
    return;

  std::lock_guard<std::mutex> lock(mutex_);
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "DELETE FROM connections WHERE side_a = ? AND side_b = ?;";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("connection_db: prepare delete failed: {}", sqlite3_errmsg(db_.get()));
    return;
  }

  sqlite3_bind_text(stmt, 1, side_a.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, side_b.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    ERROR("connection_db: delete failed: {}", sqlite3_errmsg(db_.get()));
  } else if (sqlite3_changes(db_.get()) > 0) {
    INFO("connection_db: deleted {} -> {}", side_a, side_b);
  }
  sqlite3_finalize(stmt);
}

bool connection_db_t::set_enabled(const std::string &side_a,
                                    const std::string &side_b, bool enabled) {
  if (!db_)
    return false;

  std::lock_guard<std::mutex> lock(mutex_);
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "UPDATE connections SET enabled = ? WHERE side_a = ? AND side_b = ?;";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("connection_db: prepare enable failed: {}", sqlite3_errmsg(db_.get()));
    return false;
  }

  sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
  sqlite3_bind_text(stmt, 2, side_a.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, side_b.c_str(), -1, SQLITE_TRANSIENT);

  const bool ok = sqlite3_step(stmt) == SQLITE_DONE &&
                  sqlite3_changes(db_.get()) > 0;
  sqlite3_finalize(stmt);
  return ok;
}

std::vector<stored_connection_t> connection_db_t::list_connections() const {
  std::vector<stored_connection_t> out;
  if (!db_)
    return out;

  std::lock_guard<std::mutex> lock(mutex_);
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT side_a, side_b, direction, enabled FROM connections;";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("connection_db: prepare select failed: {}", sqlite3_errmsg(db_.get()));
    return out;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    stored_connection_t row;
    if (const char *a = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)))
      row.side_a = a;
    if (const char *b = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)))
      row.side_b = b;
    if (const char *dir =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)))
      row.direction = connection_direction_from_wire(dir);
    row.enabled = sqlite3_column_int(stmt, 3) != 0;
    if (!row.side_a.empty() && !row.side_b.empty())
      out.push_back(std::move(row));
  }
  sqlite3_finalize(stmt);
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

  INFO("connection_db: attached to router (persist + auto-reconnect enabled)");
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
  INFO("connection_db: attached to ALSA sequencer (pure-ALSA pair "
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
    device.legacy_stable_id = compute_stable_id(row);
    out.push_back(std::move(device));
  }
  return out;
}

std::optional<std::string>
connection_db_manager_t::stable_id_for_peer(peer_id_t peer_id) const {
  if (!router_)
    return std::nullopt;
  const auto peer = router_->get_peer_by_id(peer_id);
  if (peer)
    return peer->compute_stable_id();
  return std::nullopt;
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
    INFO("connection_db: restoring directed edge peer {} -> {}", action.from,
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
    INFO("connection_db: storing live router edge {} -> {}", row.side_a,
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
      INFO("connection_db: removing live router edge {} -> {}",
           id_from->serialize(), id_to->serialize());
      db_->remove_connection(id_from->serialize(), id_to->serialize());
      return;
    }
  }

  const auto legacy_from = stable_id_for_peer(from);
  const auto legacy_to = stable_id_for_peer(to);
  if (legacy_from && legacy_to) {
    auto a = *legacy_from;
    auto b = *legacy_to;
    if (b < a)
      std::swap(a, b);
    db_->remove_connection(a, b);
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
  INFO("connection_db: startup reconnect scan ({} peer(s), {} saved "
       "connection(s))",
       router_->peer_ids().size(), saved.size());
  apply_saved_connections();
  try_auto_aconnect_all_alsa_pairs();
}

void connection_db_manager_t::try_auto_aconnect_all_alsa_pairs() {
  if (!db_ || !db_->is_open() || !aseq_)
    return;

  const auto saved = db_->list_connections();
  std::vector<alsa_seq_port_row_t> ports;
  bool ports_loaded = false;
  auto ensure_ports = [&]() {
    if (ports_loaded)
      return;
    ports = aseq_->enumerate_exported_ports();
    ports_loaded = true;
  };

  for (const auto &pair : saved) {
    if (!pair.enabled)
      continue;
    if (!is_direct_alsa_side(pair.side_a) || !is_direct_alsa_side(pair.side_b))
      continue;
    ensure_ports();
    const auto a_names = alsa_side_names(pair.side_a);
    const auto b_names = alsa_side_names(pair.side_b);
    if (!a_names || !b_names)
      continue;
    const auto port_a =
        find_alsa_port_by_names(ports, a_names->first, a_names->second);
    const auto port_b =
        find_alsa_port_by_names(ports, b_names->first, b_names->second);
    if (!port_a || !port_b)
      continue;

    const auto maybe_connect = [&](const aseq_t::port_t &from,
                                   const aseq_t::port_t &to) {
      if (alsa_is_already_connected(*aseq_, from, to))
        return;
      try {
        aseq_->connect_external(from, to);
      } catch (const std::exception &e) {
        ERROR("connection_db: aconnect failed: {}", e.what());
      }
    };

    if (pair.direction == connection_direction_e::a2b ||
        pair.direction == connection_direction_e::both)
      maybe_connect(*port_a, *port_b);
    if (pair.direction == connection_direction_e::b2a ||
        pair.direction == connection_direction_e::both)
      maybe_connect(*port_b, *port_a);
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
  INFO("connection_db: store requested {} <-> {}", side_a, side_b);
  db_->record_connection(side_a, side_b);
  check_reconnects_for_all();
}

void connection_db_manager_t::remove_stable_pair(const std::string &side_a,
                                                 const std::string &side_b) {
  if (!db_ || !db_->is_open())
    return;
  INFO("connection_db: delete requested {} <-> {}", side_a, side_b);
  auto a = side_a;
  auto b = side_b;
  if (b < a)
    std::swap(a, b);
  db_->remove_connection(a, b);
}

} // namespace rtpmididns

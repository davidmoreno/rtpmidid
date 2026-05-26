/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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

#include "connection_db.hpp"
#include "aseq.hpp"
#include "rtpmidid/logger.hpp"
#include <algorithm>
#include <sqlite3.h>
#include <utility>

namespace rtpmididns {

namespace {

std::string escape_stable_component(std::string s) {
  for (char &c : s) {
    if (c == ':')
      c = '|';
  }
  return s;
}

std::string unescape_stable_component(std::string s) {
  for (char &c : s) {
    if (c == '|')
      c = ':';
  }
  return s;
}

std::optional<std::string> make_stable_id(std::string prefix,
                                          const std::vector<std::string> &parts) {
  for (const auto &p : parts) {
    if (p.empty())
      return std::nullopt;
  }
  std::string out = std::move(prefix);
  for (const auto &p : parts) {
    out += ':';
    out += escape_stable_component(p);
  }
  return out;
}

/** Decode `alsa:<client_name>:<port_name>` (with `|` -> `:` unescaping) into the
 *  raw client/port name pair. Returns nullopt for any other prefix or malformed
 *  ids. The stable id format is produced by `compute_stable_id` for
 *  `local_alsa_peer_t` and `resolve_side_to_stable_id` for `alsa:<c>:<p>`. */
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

bool is_alsa_stable_id(const std::string &stable_id) {
  return parse_alsa_stable_id(stable_id).has_value();
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

} // namespace

namespace {

/** `network_address_t::hostname()` formats unresolved sockaddrs as the literal
 *  string "null". Treat it as empty so we don't burn it into a stable id. */
bool is_real_hostname(const std::string &h) {
  return !h.empty() && h != "null";
}

} // namespace

std::optional<std::string> compute_stable_id(const router_peer_row_t &row) {
  const std::string type = row.type.value_or("");
  const std::string peer_name =
      row.name && !row.name->empty() ? *row.name : std::string();

  /* Per-type structural id first. When that's unavailable, fall through to the
     generic `<short_type>:<peer_name>` fallback below so the row stays
     saveable. Caveat: peer names that include random suffixes (e.g.
     `WEB:foo:abc123`) won't survive a restart, but neither do the underlying
     peers; for INI / RPC-configured peers the name IS the stable identity. */

  if (type == "local_alsa_peer_t") {
    if (row.alsa_subscribe_from) {
      const auto &s = *row.alsa_subscribe_from;
      const auto sid = make_stable_id("alsa", {s.client_name, s.port_name});
      if (sid)
        return sid;
    }
    /* Local ALSA peer with no external subscription (e.g. a "Network" port on
       this client). Identify by configured name. */
    if (!peer_name.empty())
      return make_stable_id("alsa_local", {peer_name});
    return std::nullopt;
  }

  if (type == "local_rawmidi_peer_t") {
    if (row.device && !row.device->empty())
      return make_stable_id("rawmidi", {*row.device});
    if (!peer_name.empty())
      return make_stable_id("rawmidi_named", {peer_name});
    return std::nullopt;
  }

  if (type == "network_rtpmidi_client_t") {
    std::string hostname;
    if (row.connect_hostname && is_real_hostname(*row.connect_hostname))
      hostname = *row.connect_hostname;
    else if (row.peer && is_real_hostname(row.peer->remote.hostname))
      hostname = row.peer->remote.hostname;

    std::string service_name;
    if (row.peer && !row.peer->remote.name.empty())
      service_name = row.peer->remote.name;
    else if (!peer_name.empty())
      service_name = peer_name;

    if (!hostname.empty() && !service_name.empty())
      return make_stable_id("rtpmidi", {hostname, service_name});
    if (!peer_name.empty())
      return make_stable_id("rtpmidi_client_named", {peer_name});
    return std::nullopt;
  }

  if (type == "network_rtpmidi_peer_t") {
    if (row.peer &&
        !row.peer->remote.name.empty() &&
        is_real_hostname(row.peer->remote.hostname)) {
      return make_stable_id(
          "rtpmidi_in", {row.peer->remote.hostname, row.peer->remote.name});
    }
    /* Inbound peer hasn't finished its handshake yet, but the daemon still
       wants a stable id so the next reconnect from the same configured name
       (when present) matches. */
    if (!peer_name.empty())
      return make_stable_id("rtpmidi_in_named", {peer_name});
    return std::nullopt;
  }

  if (type == "network_rtpmidi_listener_t") {
    if (!peer_name.empty())
      return make_stable_id("rtpmidi_server", {peer_name});
    return std::nullopt;
  }

  if (type == "local_alsa_listener_t") {
    if (peer_name.empty())
      return std::nullopt;
    const auto pos = peer_name.find(" <-> ");
    if (pos != std::string::npos) {
      const std::string remote = peer_name.substr(pos + 5);
      if (!remote.empty())
        return make_stable_id("alsa_listener", {remote});
    }
    /* Name doesn't follow the " <-> " convention - still address it by the
       full name so explicit user saves work. */
    return make_stable_id("alsa_listener_named", {peer_name});
  }

  if (type == "network_rtpmidi_multi_listener_t") {
    std::string name;
    if (row.listening && !row.listening->name.empty())
      name = row.listening->name;
    else
      name = peer_name;
    if (name.empty())
      return std::nullopt;
    return make_stable_id("rtpmidi_multi", {name});
  }

  if (type == "local_alsa_multi_listener_t") {
    if (peer_name.empty())
      return std::nullopt;
    return make_stable_id("alsa_multi", {peer_name});
  }

  /* webui_midi_monitor_peer_t: explicitly not saveable - the sink is created
     per-session by monitor.start and tied to a uuid that changes every time. */
  if (type == "webui_midi_monitor_peer_t")
    return std::nullopt;

  /* Last-resort fallback for any peer type we don't recognise: address it by
     its configured name so user-initiated saves still work. Auto-save via
     `on_connected` will populate the db with this id; if the peer's name is
     stable across restarts the auto-reconnect picks it up, otherwise the
     stale row is harmless (no matching peer comes back). */
  if (!type.empty() && !peer_name.empty()) {
    /* Short prefix for readability: drop trailing "_t" if present. */
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
  }
  return std::nullopt;
}

std::pair<std::string, std::string>
connection_db_t::normalize_sides(std::string a, std::string b) {
  if (b < a)
    std::swap(a, b);
  return {std::move(a), std::move(b)};
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

  const char *schema =
      "CREATE TABLE IF NOT EXISTS connections ("
      "  side_a TEXT NOT NULL,"
      "  side_b TEXT NOT NULL,"
      "  PRIMARY KEY (side_a, side_b)"
      ");";

  char *errmsg = nullptr;
  if (sqlite3_exec(db_.get(), schema, nullptr, nullptr, &errmsg) != SQLITE_OK) {
    ERROR("connection_db: schema init failed: {}",
          errmsg ? errmsg : "unknown error");
    sqlite3_free(errmsg);
    db_.reset();
    return;
  }

  INFO("connection_db: opened {}", path);
}

void connection_db_t::record_connection(const std::string &side_a,
                                        const std::string &side_b) {
  if (!db_)
    return;

  auto sides = normalize_sides(side_a, side_b);
  std::lock_guard<std::mutex> lock(mutex_);

  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT OR REPLACE INTO connections (side_a, side_b) VALUES (?, ?);";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("connection_db: prepare insert failed: {}", sqlite3_errmsg(db_.get()));
    return;
  }

  sqlite3_bind_text(stmt, 1, sides.first.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, sides.second.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    ERROR("connection_db: insert failed: {}", sqlite3_errmsg(db_.get()));
  } else {
    INFO("connection_db: stored in database {} <-> {}", sides.first,
         sides.second);
  }
  sqlite3_finalize(stmt);
}

void connection_db_t::remove_connection(const std::string &side_a,
                                        const std::string &side_b) {
  if (!db_)
    return;

  auto sides = normalize_sides(side_a, side_b);
  std::lock_guard<std::mutex> lock(mutex_);

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "DELETE FROM connections WHERE side_a = ? AND side_b = ?;";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("connection_db: prepare delete failed: {}", sqlite3_errmsg(db_.get()));
    return;
  }

  sqlite3_bind_text(stmt, 1, sides.first.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, sides.second.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    ERROR("connection_db: delete failed: {}", sqlite3_errmsg(db_.get()));
  } else if (sqlite3_changes(db_.get()) > 0) {
    INFO("connection_db: deleted from database {} <-> {}", sides.first,
         sides.second);
  } else {
    INFO("connection_db: delete had no matching row for {} <-> {}",
         sides.first, sides.second);
  }
  sqlite3_finalize(stmt);
}

std::vector<connection_pair_t> connection_db_t::get_connections() const {
  std::vector<connection_pair_t> out;
  if (!db_)
    return out;

  std::lock_guard<std::mutex> lock(mutex_);

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT side_a, side_b FROM connections;";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("connection_db: prepare select failed: {}", sqlite3_errmsg(db_.get()));
    return out;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    connection_pair_t pair;
    if (const char *a = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)))
      pair.side_a = a;
    if (const char *b = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)))
      pair.side_b = b;
    if (!pair.side_a.empty() && !pair.side_b.empty())
      out.push_back(std::move(pair));
  }
  sqlite3_finalize(stmt);
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
        // Any new external ALSA port might be one half of a saved pair.
        try_auto_aconnect_all_alsa_pairs();
      });
}

std::optional<std::string>
connection_db_manager_t::stable_id_for_peer(peer_id_t peer_id) const {
  if (!router_)
    return std::nullopt;
  for (const auto &row : router_->status_rows()) {
    if (row.id && static_cast<peer_id_t>(*row.id) == peer_id)
      return compute_stable_id(row);
  }
  return std::nullopt;
}

std::optional<peer_id_t>
connection_db_manager_t::find_peer_by_stable_id(const std::string &stable_id) const {
  if (!router_)
    return std::nullopt;
  for (const auto &row : router_->status_rows()) {
    const auto sid = compute_stable_id(row);
    if (sid && *sid == stable_id && row.id)
      return static_cast<peer_id_t>(*row.id);
  }
  return std::nullopt;
}

void connection_db_manager_t::try_record_pair(peer_id_t a, peer_id_t b) {
  if (!db_ || !db_->is_open())
    return;

  const auto id_a = stable_id_for_peer(a);
  const auto id_b = stable_id_for_peer(b);

  if (id_a && id_b) {
    INFO("connection_db: storing live router edge peer {} ({}) <-> peer {} ({})",
         a, *id_a, b, *id_b);
    db_->record_connection(*id_a, *id_b);
    std::lock_guard<std::mutex> lock(state_mutex_);
    pending_records_.erase(
        std::remove_if(pending_records_.begin(), pending_records_.end(),
                       [a, b](const pending_pair_t &p) {
                         return (p.a == a && p.b == b) || (p.a == b && p.b == a);
                       }),
        pending_records_.end());
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto &p : pending_records_) {
      if ((p.a == a && p.b == b) || (p.a == b && p.b == a))
        return;
    }
    pending_records_.push_back(pending_pair_t{a, b});
  }

  if (!id_a && !id_b)
    INFO("connection_db: deferred recording for peers {} <-> {} (stable ids pending)",
         a, b);
  else if (!id_a)
    INFO("connection_db: deferred recording for peer {} (stable id pending)", a);
  else
    INFO("connection_db: deferred recording for peer {} (stable id pending)", b);
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

static bool router_has_edge(const std::shared_ptr<midirouter_t> &router,
                            peer_id_t from, peer_id_t to) {
  const auto targets = router->send_targets_for(from);
  return std::find(targets.begin(), targets.end(), to) != targets.end();
}

void connection_db_manager_t::try_auto_connect_peer(peer_id_t peer_id) {
  if (!db_ || !db_->is_open() || !router_)
    return;

  DEBUG("connection_db: auto-connect check for peer_id={}", peer_id);

  const auto my_id = stable_id_for_peer(peer_id);
  if (!my_id) {
    DEBUG("connection_db: peer_id={} has no stable id yet, skip auto-connect",
          peer_id);
    return;
  }

  const auto saved = db_->get_connections();
  DEBUG("connection_db: peer_id={} stable_id={} scanning {} saved pair(s)",
        peer_id, *my_id, saved.size());

  for (const auto &pair : saved) {
    std::optional<std::string> other_stable;
    if (pair.side_a == *my_id)
      other_stable = pair.side_b;
    else if (pair.side_b == *my_id)
      other_stable = pair.side_a;
    else
      continue;

    if (!other_stable)
      continue;

    const auto other_peer = find_peer_by_stable_id(*other_stable);
    if (!other_peer) {
      DEBUG("connection_db: saved pair {} <-> {} — other endpoint {} not online",
            *my_id, *other_stable, *other_stable);
      continue;
    }
    if (*other_peer == peer_id)
      continue;

    if (router_has_edge(router_, peer_id, *other_peer) &&
        router_has_edge(router_, *other_peer, peer_id)) {
      DEBUG("connection_db: saved pair {} <-> {} already routed (peers {} <-> "
            "{})",
            *my_id, *other_stable, peer_id, *other_peer);
      continue;
    }

    INFO("connection_db: restoring from database {} <-> {} (router peers {} "
         "<-> {})",
         *my_id, *other_stable, peer_id, *other_peer);
    router_->enqueue_connect(peer_id, *other_peer);
    router_->enqueue_connect(*other_peer, peer_id);
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

  const auto id_from = stable_id_for_peer(from);
  const auto id_to = stable_id_for_peer(to);
  if (id_from && id_to) {
    INFO("connection_db: removing live router edge peer {} ({}) <-> peer {} ({})",
         from, *id_from, to, *id_to);
    db_->remove_connection(*id_from, *id_to);
  }
}

void connection_db_manager_t::on_peer_added(peer_id_t peer_id) {
  DEBUG("connection_db: peer_added peer_id={} — running auto-connect check",
        peer_id);
  try_auto_connect_peer(peer_id);
}

void connection_db_manager_t::on_peer_event(peer_id_t peer_id,
                                           midipeer_event_e evt) {
  if (evt == midipeer_event_e::CONNECTED_PEER) {
    DEBUG("connection_db: peer_id={} CONNECTED_PEER — finalize pending + "
          "auto-connect",
          peer_id);
    try_finalize_pending_for(peer_id);
    try_auto_connect_peer(peer_id);
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
  const auto ids = router_->peer_ids();
  const auto saved = db_ && db_->is_open() ? db_->get_connections() : std::vector<connection_pair_t>{};
  INFO("connection_db: startup reconnect scan ({} peer(s), {} saved pair(s))",
       ids.size(), saved.size());
  for (const auto id : ids)
    try_auto_connect_peer(id);
  try_auto_aconnect_all_alsa_pairs();
}

void connection_db_manager_t::try_auto_aconnect_all_alsa_pairs() {
  if (!db_ || !db_->is_open() || !aseq_)
    return;

  const auto saved = db_->get_connections();
  std::vector<alsa_seq_port_row_t> ports;
  bool ports_loaded = false;
  /* Lazily enumerate ports the first time we hit an ALSA-only saved pair so
     the sweep is free when no such pairs are stored. */
  auto ensure_ports = [&]() {
    if (ports_loaded)
      return;
    ports = aseq_->enumerate_exported_ports();
    ports_loaded = true;
  };

  for (const auto &pair : saved) {
    if (!is_alsa_stable_id(pair.side_a) || !is_alsa_stable_id(pair.side_b))
      continue;
    ensure_ports();
    const auto a_names = parse_alsa_stable_id(pair.side_a);
    const auto b_names = parse_alsa_stable_id(pair.side_b);
    if (!a_names || !b_names)
      continue;
    const auto port_a =
        find_alsa_port_by_names(ports, a_names->first, a_names->second);
    const auto port_b =
        find_alsa_port_by_names(ports, b_names->first, b_names->second);
    if (!port_a || !port_b) {
      DEBUG("connection_db: saved ALSA pair {} <-> {} - waiting for both "
            "ports to come online",
            pair.side_a, pair.side_b);
      continue;
    }

    if (!alsa_is_already_connected(*aseq_, *port_a, *port_b)) {
      INFO("connection_db: aconnect saved ALSA pair {} <-> {} ({}:{} -> "
           "{}:{})",
           pair.side_a, pair.side_b, port_a->client, port_a->port,
           port_b->client, port_b->port);
      try {
        aseq_->connect_external(*port_a, *port_b);
      } catch (const std::exception &e) {
        ERROR("connection_db: aconnect failed {} -> {}: {}", pair.side_a,
              pair.side_b, e.what());
      }
    }
    if (!alsa_is_already_connected(*aseq_, *port_b, *port_a)) {
      INFO("connection_db: aconnect saved ALSA pair {} <-> {} ({}:{} -> "
           "{}:{}) [reverse]",
           pair.side_a, pair.side_b, port_b->client, port_b->port,
           port_a->client, port_a->port);
      try {
        aseq_->connect_external(*port_b, *port_a);
      } catch (const std::exception &e) {
        ERROR("connection_db: aconnect failed {} -> {}: {}", pair.side_b,
              pair.side_a, e.what());
      }
    }
  }
}

void connection_db_manager_t::record_stable_pair(const std::string &side_a,
                                                 const std::string &side_b) {
  if (!db_ || !db_->is_open())
    return;
  INFO("connection_db: store requested {} <-> {}", side_a, side_b);
  db_->record_connection(side_a, side_b);
  /* check_reconnects_for_all() already sweeps ALSA pairs, so a newly stored
     ALSA pair is materialised immediately if both ports are present. */
  check_reconnects_for_all();
}

void connection_db_manager_t::remove_stable_pair(const std::string &side_a,
                                                 const std::string &side_b) {
  if (!db_ || !db_->is_open())
    return;
  INFO("connection_db: delete requested {} <-> {}", side_a, side_b);
  db_->remove_connection(side_a, side_b);
}

} // namespace rtpmididns

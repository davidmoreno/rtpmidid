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

#pragma once

#include "aseq.hpp"
#include "dm_json_status.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/signal.hpp"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sqlite3.h>

namespace rtpmididns {

struct sqlite3_deleter {
  void operator()(sqlite3 *db) const noexcept {
    if (db != nullptr)
      sqlite3_close(db);
  }
};

using sqlite3_db = std::unique_ptr<sqlite3, sqlite3_deleter>;

/** Stable endpoint key for a peer status row (nullopt if not yet known). */
std::optional<std::string> compute_stable_id(const router_peer_row_t &row);

/** Find a router peer id whose stable id matches @a stable_id (current status snapshot). */
std::optional<peer_id_t>
find_peer_id_for_stable_id(const std::vector<router_peer_row_t> &rows,
                           const std::string &stable_id);

struct connection_pair_t {
  std::string side_a;
  std::string side_b;
};

/** SQLite persistence for router connection edges. */
class connection_db_t {
  NON_COPYABLE_NOR_MOVABLE(connection_db_t)

public:
  explicit connection_db_t(std::string path);
  ~connection_db_t() = default;

  bool is_open() const { return db_ != nullptr; }

  void record_connection(const std::string &side_a, const std::string &side_b);
  void remove_connection(const std::string &side_a, const std::string &side_b);
  std::vector<connection_pair_t> get_connections() const;

private:
  sqlite3_db db_;
  mutable std::mutex mutex_;

  static std::pair<std::string, std::string> normalize_sides(std::string a,
                                                             std::string b);
};

/** Hooks router events to persist and restore connections from the database. */
class connection_db_manager_t {
  NON_COPYABLE_NOR_MOVABLE(connection_db_manager_t)

public:
  connection_db_manager_t(std::shared_ptr<midirouter_t> router,
                          std::unique_ptr<connection_db_t> db);
  ~connection_db_manager_t() = default;

  void attach();
  /** Wire ALSA-side auto-reconnect: aconnect saved pure-ALSA pairs when both
   *  ports are available. Hooks aseq->added_port_announcement. */
  void attach_aseq(std::shared_ptr<aseq_t> aseq);
  void check_reconnects_for_all();

  connection_db_t &database() { return *db_; }
  const connection_db_t &database() const { return *db_; }

  /** Persist a stable-id pair and attempt router auto-connect for matching peers. */
  void record_stable_pair(const std::string &side_a, const std::string &side_b);
  void remove_stable_pair(const std::string &side_a, const std::string &side_b);

private:
  std::shared_ptr<midirouter_t> router_;
  std::unique_ptr<connection_db_t> db_;
  std::shared_ptr<aseq_t> aseq_;
  std::mutex state_mutex_;

  rtpmidid::connection_t<peer_id_t, peer_id_t> connected_connection_;
  rtpmidid::connection_t<peer_id_t, peer_id_t> disconnected_connection_;
  rtpmidid::connection_t<peer_id_t> peer_added_connection_;
  rtpmidid::connection_t<peer_id_t, midipeer_event_e> peer_event_connection_;
  rtpmidid::connection_t<const std::string &, aseq_t::client_type_e,
                         const aseq_t::port_t &>
      aseq_port_added_connection_;

  struct pending_pair_t {
    peer_id_t a = 0;
    peer_id_t b = 0;
  };
  std::vector<pending_pair_t> pending_records_;

  std::optional<std::string> stable_id_for_peer(peer_id_t peer_id) const;
  std::optional<peer_id_t> find_peer_by_stable_id(const std::string &stable_id) const;

  void try_record_pair(peer_id_t a, peer_id_t b);
  void try_finalize_pending_for(peer_id_t peer_id);
  void try_auto_connect_peer(peer_id_t peer_id);
  /** Sweep all saved pairs and aconnect any pure-ALSA pair whose both ports
   *  are now present (no-op otherwise). */
  void try_auto_aconnect_all_alsa_pairs();

  void on_connected(peer_id_t from, peer_id_t to);
  void on_disconnected(peer_id_t from, peer_id_t to);
  void on_peer_added(peer_id_t peer_id);
  void on_peer_event(peer_id_t peer_id, midipeer_event_e evt);
};

} // namespace rtpmididns

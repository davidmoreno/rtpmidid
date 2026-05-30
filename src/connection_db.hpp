/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
 */
#pragma once

#include "aseq.hpp"
#include "device_identity.hpp"
#include "dm_json_status.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/signal.hpp"
#include "sqlite_db.hpp"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rtpmididns {

enum class connection_direction_e { a2b, b2a, both };

connection_direction_e connection_direction_from_wire(std::string_view wire);
const char *connection_direction_to_wire(connection_direction_e direction);
connection_direction_e flip_connection_direction(connection_direction_e direction);

struct stored_connection_t {
  std::string side_a;
  std::string side_b;
  connection_direction_e direction = connection_direction_e::both;
  bool enabled = true;
};

/** Sort side_a/side_b lexicographically; flip direction when sides swap. */
stored_connection_t canonicalize_stored_connection(stored_connection_t connection);

/** Legacy row without direction metadata. */
struct connection_pair_t {
  std::string side_a;
  std::string side_b;
};

class connection_db_t {
  NON_COPYABLE_NOR_MOVABLE(connection_db_t)

public:
  explicit connection_db_t(std::string path);
  ~connection_db_t() = default;

  bool is_open() const { return db_.is_open(); }

  void save_connection(const stored_connection_t &connection);
  void remove_connection(const std::string &side_a, const std::string &side_b);
  bool set_enabled(const std::string &side_a, const std::string &side_b,
                   bool enabled);
  std::vector<stored_connection_t> list_connections() const;

  /** Legacy API: stores direction=both with sorted sides. */
  void record_connection(const std::string &side_a, const std::string &side_b);
  std::vector<connection_pair_t> get_connections() const;

private:
  sqlite_db_t db_;

  void migrate_schema();
  static std::pair<std::string, std::string> normalize_sides(std::string a,
                                                             std::string b);
};

struct online_device_t;

class connection_db_manager_t {
  NON_COPYABLE_NOR_MOVABLE(connection_db_manager_t)

public:
  connection_db_manager_t(std::shared_ptr<midirouter_t> router,
                          std::unique_ptr<connection_db_t> db);
  ~connection_db_manager_t() = default;

  void attach();
  void attach_aseq(std::shared_ptr<aseq_t> aseq);
  void check_reconnects_for_all();

  connection_db_t &database() { return *db_; }
  const connection_db_t &database() const { return *db_; }

  void record_stable_pair(const std::string &side_a, const std::string &side_b);
  void remove_stable_pair(const std::string &side_a, const std::string &side_b);
  void save_stored_connection(stored_connection_t connection);
  bool set_stored_enabled(const std::string &side_a, const std::string &side_b,
                          bool enabled);

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

  std::vector<online_device_t> collect_online_devices() const;
  std::optional<device_identity_t>
  device_identity_for_peer(peer_id_t peer_id) const;

  void apply_saved_connections();
  void try_record_pair(peer_id_t from, peer_id_t to);
  void try_finalize_pending_for(peer_id_t peer_id);
  void try_auto_aconnect_all_alsa_pairs();

  void on_connected(peer_id_t from, peer_id_t to);
  void on_disconnected(peer_id_t from, peer_id_t to);
  void on_peer_added(peer_id_t peer_id);
  void on_peer_event(peer_id_t peer_id, midipeer_event_e evt);
};

} // namespace rtpmididns

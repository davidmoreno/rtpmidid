/**
 * Phase 4: devices table persistence.
 */
#include "device_db.hpp"

#include "device_registry.hpp"

#include "rtpmidid/logger.hpp"
#include <sqlite3.h>
#include <cstring>
#include <utility>

namespace rtpmididns {

namespace {

const char *source_to_wire(device_source_e source) {
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

std::optional<device_source_e> source_from_wire(const char *wire) {
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

} // namespace

void device_sqlite3_deleter::operator()(sqlite3 *db) const noexcept {
  if (db != nullptr)
    sqlite3_close(db);
}

device_db_t::device_db_t(std::string path) {
  if (path.empty())
    return;

  sqlite3 *raw = nullptr;
  const int rc = sqlite3_open(path.c_str(), &raw);
  if (rc != SQLITE_OK) {
    ERROR("device_db: cannot open {}: {}", path,
          raw ? sqlite3_errmsg(raw) : "unknown error");
    device_sqlite3_deleter{}(raw);
    return;
  }
  db_.reset(raw);

  const char *schema =
      "CREATE TABLE IF NOT EXISTS devices ("
      "  identity   TEXT PRIMARY KEY,"
      "  type       TEXT NOT NULL,"
      "  name       TEXT,"
      "  source     TEXT NOT NULL,"
      "  first_seen INTEGER NOT NULL,"
      "  last_seen  INTEGER NOT NULL"
      ");";

  char *errmsg = nullptr;
  if (sqlite3_exec(db_.get(), schema, nullptr, nullptr, &errmsg) != SQLITE_OK) {
    ERROR("device_db: schema init failed: {}", errmsg ? errmsg : "unknown error");
    sqlite3_free(errmsg);
    db_.reset();
    return;
  }

  INFO("device_db: opened {}", path);
}

device_db_t::~device_db_t() = default;

std::vector<device_record_t> device_db_t::load_all() const {
  std::vector<device_record_t> out;
  if (!db_)
    return out;

  std::lock_guard<std::mutex> lock(mutex_);

  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT identity, name, source, first_seen, last_seen FROM devices;";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("device_db: prepare select failed: {}", sqlite3_errmsg(db_.get()));
    return out;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *identity =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    if (!identity)
      continue;
    const auto parsed = device_identity_t::parse(identity);
    if (!parsed)
      continue;

    device_record_t rec;
    rec.identity = *parsed;
    if (const char *name =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)))
      rec.display_name = name;
    const auto source = source_from_wire(reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 2)));
    if (!source)
      continue;
    rec.source = *source;
    rec.first_seen = sqlite3_column_int64(stmt, 3);
    rec.last_seen = sqlite3_column_int64(stmt, 4);
    out.push_back(std::move(rec));
  }
  sqlite3_finalize(stmt);
  return out;
}

void device_db_t::upsert(const device_record_t &record) {
  if (!db_)
    return;

  const std::string key = record.identity_key();
  std::lock_guard<std::mutex> lock(mutex_);

  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT OR REPLACE INTO devices "
      "(identity, type, name, source, first_seen, last_seen) "
      "VALUES (?, ?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("device_db: prepare upsert failed: {}", sqlite3_errmsg(db_.get()));
    return;
  }

  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, record.identity.type_prefix.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, record.display_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, source_to_wire(record.source), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, record.first_seen);
  sqlite3_bind_int64(stmt, 6, record.last_seen);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    ERROR("device_db: upsert failed: {}", sqlite3_errmsg(db_.get()));
  }
  sqlite3_finalize(stmt);
}

void device_db_t::remove(const std::string &identity_key) {
  if (!db_)
    return;

  std::lock_guard<std::mutex> lock(mutex_);

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "DELETE FROM devices WHERE identity = ?;";
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ERROR("device_db: prepare delete failed: {}", sqlite3_errmsg(db_.get()));
    return;
  }

  sqlite3_bind_text(stmt, 1, identity_key.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    ERROR("device_db: delete failed: {}", sqlite3_errmsg(db_.get()));
  }
  sqlite3_finalize(stmt);
}

} // namespace rtpmididns

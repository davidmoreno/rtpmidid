/**
 * Phase 4: devices table persistence.
 */
#include "device_db.hpp"

#include "device_registry.hpp"

#include "rtpmidid/logger.hpp"
#include <sqlite3.h>
#include <utility>

namespace rtpmididns {

device_db_t::device_db_t(std::string path) : db_(std::move(path)) {
  if (!db_.is_open())
    return;

  const char *schema =
      "CREATE TABLE IF NOT EXISTS devices ("
      "  identity   TEXT PRIMARY KEY,"
      "  type       TEXT NOT NULL,"
      "  name       TEXT,"
      "  source     TEXT NOT NULL,"
      "  first_seen INTEGER NOT NULL,"
      "  last_seen  INTEGER NOT NULL"
      ");";

  if (!db_.exec(schema, "device_db schema init")) {
    db_.close();
    return;
  }

  INFO("device_db: opened");
}

std::vector<device_record_t> device_db_t::load_all() const {
  std::vector<device_record_t> out;
  if (!db_.is_open())
    return out;

  auto lock = db_.lock();
  auto stmt = db_.prepare(
      "SELECT identity, name, source, first_seen, last_seen FROM devices;",
      "device_db select");
  if (!stmt)
    return out;

  while (stmt->step() == SQLITE_ROW) {
    const auto identity = stmt->column_text(0);
    if (!identity)
      continue;
    const auto parsed = device_identity_t::parse(*identity);
    if (!parsed)
      continue;

    device_record_t rec;
    rec.identity = *parsed;
    if (const auto name = stmt->column_text(1))
      rec.display_name = std::string{*name};
    const auto source = device_source_from_wire(
        stmt->column_text(2) ? stmt->column_text(2)->data() : nullptr);
    if (!source)
      continue;
    rec.source = *source;
    rec.first_seen = stmt->column_int64(3);
    rec.last_seen = stmt->column_int64(4);
    out.push_back(std::move(rec));
  }
  return out;
}

void device_db_t::upsert(const device_record_t &record) {
  if (!db_.is_open())
    return;

  const std::string key = record.identity_key();
  auto lock = db_.lock();
  auto stmt = db_.prepare(
      "INSERT OR REPLACE INTO devices "
      "(identity, type, name, source, first_seen, last_seen) "
      "VALUES (?, ?, ?, ?, ?, ?);",
      "device_db upsert");
  if (!stmt)
    return;

  stmt->bind_text(1, key);
  stmt->bind_text(2, record.identity.type_prefix);
  stmt->bind_text(3, record.display_name);
  stmt->bind_text(4, device_source_to_wire(record.source));
  stmt->bind_int64(5, record.first_seen);
  stmt->bind_int64(6, record.last_seen);

  if (stmt->step() != SQLITE_DONE) {
    ERROR("device_db: upsert failed: {}", sqlite3_errmsg(db_.raw()));
  }
}

void device_db_t::remove(const std::string &identity_key) {
  if (!db_.is_open())
    return;

  auto lock = db_.lock();
  auto stmt =
      db_.prepare("DELETE FROM devices WHERE identity = ?;", "device_db delete");
  if (!stmt)
    return;

  stmt->bind_text(1, identity_key);
  if (stmt->step() != SQLITE_DONE) {
    ERROR("device_db: delete failed");
  }
}

} // namespace rtpmididns

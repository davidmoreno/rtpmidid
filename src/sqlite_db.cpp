/**
 * RAII SQLite helpers shared by connection_db and device_db.
 */
#include "sqlite_db.hpp"

#include "rtpmidid/logger.hpp"
#include <sqlite3.h>

namespace rtpmididns {

void sqlite3_deleter::operator()(sqlite3 *db) const noexcept {
  if (db != nullptr)
    sqlite3_close(db);
}

sqlite_stmt_t::~sqlite_stmt_t() {
  if (stmt_ != nullptr)
    sqlite3_finalize(stmt_);
}

sqlite_stmt_t::sqlite_stmt_t(sqlite_stmt_t &&other) noexcept
    : db_(other.db_), stmt_(other.stmt_) {
  other.db_ = nullptr;
  other.stmt_ = nullptr;
}

sqlite_stmt_t &sqlite_stmt_t::operator=(sqlite_stmt_t &&other) noexcept {
  if (this != &other) {
    if (stmt_ != nullptr)
      sqlite3_finalize(stmt_);
    db_ = other.db_;
    stmt_ = other.stmt_;
    other.db_ = nullptr;
    other.stmt_ = nullptr;
  }
  return *this;
}

void sqlite_stmt_t::bind_text(int index, std::string_view value) {
  sqlite3_bind_text(stmt_, index, value.data(),
                    static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void sqlite_stmt_t::bind_int(int index, int value) {
  sqlite3_bind_int(stmt_, index, value);
}

void sqlite_stmt_t::bind_int64(int index, int64_t value) {
  sqlite3_bind_int64(stmt_, index, value);
}

int sqlite_stmt_t::step() { return sqlite3_step(stmt_); }

std::optional<std::string_view> sqlite_stmt_t::column_text(int index) const {
  const char *text =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt_, index));
  if (!text)
    return std::nullopt;
  return std::string_view{text};
}

int sqlite_stmt_t::column_int(int index) const {
  return sqlite3_column_int(stmt_, index);
}

int64_t sqlite_stmt_t::column_int64(int index) const {
  return sqlite3_column_int64(stmt_, index);
}

sqlite_db_t::sqlite_db_t(std::string path) {
  if (path.empty())
    return;

  sqlite3 *raw = nullptr;
  const int rc = sqlite3_open(path.c_str(), &raw);
  if (rc != SQLITE_OK) {
    ERROR("sqlite_db: cannot open {}: {}", path,
          raw ? sqlite3_errmsg(raw) : "unknown error");
    sqlite3_deleter{}(raw);
    return;
  }
  sqlite3_busy_timeout(raw, 5000);
  db_.reset(raw);
}

int sqlite_db_t::changes() const {
  if (!db_)
    return 0;
  return sqlite3_changes(db_.get());
}

void sqlite_db_t::close() { db_.reset(); }

bool sqlite_db_t::exec(const char *sql, const char *log_context) {
  if (!db_)
    return false;

  char *errmsg = nullptr;
  if (sqlite3_exec(db_.get(), sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
    ERROR("sqlite_db: {} failed: {}", log_context,
          errmsg ? errmsg : "unknown error");
    sqlite3_free(errmsg);
    return false;
  }
  return true;
}

std::optional<sqlite_stmt_t> sqlite_db_t::prepare(const char *sql,
                                                  const char *log_context) const {
  if (!db_)
    return std::nullopt;

  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(db_.get(), sql, -1, &raw, nullptr) != SQLITE_OK) {
    ERROR("sqlite_db: {} prepare failed: {}", log_context,
          sqlite3_errmsg(db_.get()));
    return std::nullopt;
  }
  return sqlite_stmt_t{db_.get(), raw};
}

bool sqlite_db_t::has_table_column(const char *table,
                                   const char *column) const {
  if (!db_ || !table || !column)
    return false;

  std::lock_guard<std::mutex> guard(mutex_);
  const std::string pragma =
      std::string("PRAGMA table_info(") + table + ");";
  auto stmt = prepare(pragma.c_str(), "sqlite_db table_info");
  if (!stmt)
    return false;

  while (stmt->step() == SQLITE_ROW) {
    const auto name = stmt->column_text(1);
    if (name && *name == column)
      return true;
  }
  return false;
}

} // namespace rtpmididns

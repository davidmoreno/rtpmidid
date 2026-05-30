/**
 * RAII SQLite helpers shared by connection_db and device_db.
 */
#pragma once

#include "utils.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace rtpmididns {

struct sqlite3_deleter {
  void operator()(sqlite3 *db) const noexcept;
};

using sqlite3_db = std::unique_ptr<sqlite3, sqlite3_deleter>;

class sqlite_db_t;

class sqlite_stmt_t {
  NON_COPYABLE(sqlite_stmt_t)

public:
  sqlite_stmt_t() = default;
  ~sqlite_stmt_t();

  sqlite_stmt_t(sqlite_stmt_t &&other) noexcept;
  sqlite_stmt_t &operator=(sqlite_stmt_t &&other) noexcept;

  explicit operator bool() const noexcept { return stmt_ != nullptr; }

  void bind_text(int index, std::string_view value);
  void bind_int(int index, int value);
  void bind_int64(int index, int64_t value);

  int step();
  std::optional<std::string_view> column_text(int index) const;
  int column_int(int index) const;
  int64_t column_int64(int index) const;

private:
  friend class sqlite_db_t;
  sqlite_stmt_t(sqlite3 *db, sqlite3_stmt *stmt) : db_(db), stmt_(stmt) {}

  sqlite3 *db_ = nullptr;
  sqlite3_stmt *stmt_ = nullptr;
};

class sqlite_db_t {
  NON_COPYABLE_NOR_MOVABLE(sqlite_db_t)

public:
  explicit sqlite_db_t(std::string path);
  ~sqlite_db_t() = default;

  bool is_open() const { return db_ != nullptr; }
  sqlite3 *raw() const { return db_.get(); }
  int changes() const;
  void close();

  bool exec(const char *sql, const char *log_context);
  std::optional<sqlite_stmt_t> prepare(const char *sql,
                                       const char *log_context) const;

  /** True when {@code table} has a column named {@code column}. */
  bool has_table_column(const char *table, const char *column) const;

  std::lock_guard<std::mutex> lock() const {
    return std::lock_guard<std::mutex>(mutex_);
  }

private:
  sqlite3_db db_;
  mutable std::mutex mutex_;
};

} // namespace rtpmididns

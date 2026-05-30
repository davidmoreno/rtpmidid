/**
 * Phase 4: SQLite persistence for the device registry.
 */
#pragma once

#include "utils.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace rtpmididns {

struct device_record_t;

struct device_sqlite3_deleter {
  void operator()(sqlite3 *db) const noexcept;
};

using device_sqlite3_db =
    std::unique_ptr<sqlite3, device_sqlite3_deleter>;

class device_db_t {
  NON_COPYABLE_NOR_MOVABLE(device_db_t)

public:
  explicit device_db_t(std::string path);
  ~device_db_t();

  bool is_open() const { return db_ != nullptr; }

  std::vector<device_record_t> load_all() const;
  void upsert(const device_record_t &record);
  void remove(const std::string &identity_key);

private:
  device_sqlite3_db db_;
  mutable std::mutex mutex_;
};

} // namespace rtpmididns

/**
 * Phase 4: SQLite persistence for the device registry.
 */
#pragma once

#include "sqlite_db.hpp"

#include <string>
#include <vector>

namespace rtpmididns {

struct device_record_t;

class device_db_t {
  NON_COPYABLE_NOR_MOVABLE(device_db_t)

public:
  explicit device_db_t(std::string path);
  ~device_db_t() = default;

  bool is_open() const { return db_.is_open(); }

  std::vector<device_record_t> load_all() const;
  void upsert(const device_record_t &record);
  void remove(const std::string &identity_key);

private:
  sqlite_db_t db_;
};

} // namespace rtpmididns

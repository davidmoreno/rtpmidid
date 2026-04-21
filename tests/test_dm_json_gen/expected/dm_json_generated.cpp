#include "dm_json_generated.hpp"

namespace rtpmididns::dmjson {

void to_json(const ::rtpmididns::golden_foo_t &o, writer_t &w) {
  w.begin_object();
  w.key("x");
  w.int_value(static_cast<int64_t>(o.x));
  w.key("y");
  w.string_value(o.y);
  w.key("raw");
  w.raw(o.raw);
  w.end_object();
}

bool from_json(reader_t &r, ::rtpmididns::golden_foo_t &o) noexcept {
  try {
    bool seen_x = false;
    bool seen_y = false;
    bool seen_raw = false;
    while (true) {
      if (r.try_consume_object_end())
        break;
      std::string key;
      r.read_string_key_into(key);
      r.expect_colon();
      bool hit = false;
      if (key == "x") {
        if (seen_x) return false;
        seen_x = true;
        o.x = static_cast<int32_t>(r.read_int64());
        hit = true;
      }
      else if (key == "y") {
        if (seen_y) return false;
        seen_y = true;
        r.read_string_into(o.y);
        hit = true;
      }
      else if (key == "raw") {
        if (seen_raw) return false;
        seen_raw = true;
        r.read_raw_into(o.raw);
        hit = true;
      }
      if (!hit)
        r.skip_value();
      if (r.try_consume_object_end())
        break;
      r.expect_comma();
    }
    return true;
  } catch (...) {
    return false;
  }
}
bool from_json(std::string_view sv, ::rtpmididns::golden_foo_t &o) noexcept {
  try {
    reader_t r(sv);
    r.expect_object_begin();
    return from_json(r, o);
  } catch (...) {
    return false;
  }
}

} // namespace rtpmididns::dmjson

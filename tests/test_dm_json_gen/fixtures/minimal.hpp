#pragma once
#include <string>
namespace rtpmididns {
// [dm-json]
struct golden_foo_t {
  int32_t x;
  std::string y;
  std::string raw; // [dm-json: opaque]
};
}

/**
 * Shared helpers for positional stable ids.
 */
#include "peer_stable_id.hpp"

namespace rtpmididns {

namespace {

std::string escape_stable_component(std::string s) {
  for (char &c : s) {
    if (c == ':')
      c = '|';
  }
  return s;
}

} // namespace

std::optional<std::string> make_stable_id(std::string prefix,
                                          const std::vector<std::string> &parts) {
  for (const auto &p : parts) {
    if (p.empty())
      return std::nullopt;
  }
  std::string out = std::move(prefix);
  for (const auto &p : parts) {
    out += ':';
    out += escape_stable_component(p);
  }
  return out;
}

bool stable_id_is_real_hostname(const std::string &hostname) {
  return !hostname.empty() && hostname != "null";
}

} // namespace rtpmididns

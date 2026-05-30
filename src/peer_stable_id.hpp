/**
 * Shared helpers for positional stable ids (Phase 5 replaces with key=value).
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rtpmididns {

std::optional<std::string> make_stable_id(std::string prefix,
                                          const std::vector<std::string> &parts);

bool stable_id_is_real_hostname(const std::string &hostname);

} // namespace rtpmididns

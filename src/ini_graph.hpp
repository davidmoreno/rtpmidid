/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * INI unified graph: peers + connects lowered into legacy settings vectors.
 */
#pragma once

#include <string>

namespace rtpmididns {
struct settings_t;
/** Call after loading INI; consumes `ini_peers` and `ini_connects`. */
void finalize_unified_ini_graph(settings_t &settings,
                                 const std::string &ini_filename);
} // namespace rtpmididns

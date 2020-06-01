/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "./stringpp.hpp"

std::vector<std::string> rtpmidid::split(std::string const &str,
                                         const char delim) {
  std::vector<std::string> ret;
  size_t I;
  size_t endI = 0;

  while ((I = str.find_first_not_of(delim, endI)) != std::string::npos) {
    endI = str.find(delim, I);
    ret.push_back(str.substr(I, endI - I));
  }
  return ret;
}

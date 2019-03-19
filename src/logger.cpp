/**
 * Real Time Protocol Music Industry Digital Interface Daemon
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
#include "logger.hpp"
#include <stdio.h>

namespace logger{
  logger __logger;

  logger::logger(){

  }
  logger::~logger(){

  }
  void logger::log(const char *filename, int lineno, LogLevel loglevel, const std::string &msg){
    char *filename2 = strdupa(filename);
    fprintf(::stderr, "%s:%d %s", basename(filename2), lineno, (msg + "\n").c_str());
  }
  void logger::flush(){
    ::fflush(::stderr);
  }
}

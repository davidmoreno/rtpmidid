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
#include "./logger.hpp"
#include "./aseq.hpp"
#include "./exceptions.hpp"
#include <fmt/format.h>
#include <stdio.h>

namespace rtpmidid::aseq{
  void error_handler(const char *file, int line, const char *function, int err, const char *fmt, ...){
    va_list arg;
    std::string msg;
    char buffer[1024];

    if (err == ENOENT)	/* Ignore those misleading "warnings" */
      return;
    va_start(arg, fmt);
    vsprintf(buffer, fmt, arg);
    msg += buffer;
    if (err){
      msg += ": ";
      msg += snd_strerror(err);
    }
    va_end(arg);
    std::string filename = "alsa/";
    filename += file;

    logger::__logger.log(filename.c_str(), line, ::logger::LogLevel::ERROR, msg);
  }

  aseq::aseq(const std::string &name){
    snd_lib_error_set_handler(error_handler);
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0){
      throw rtpmidid::exception("Can't open sequencer. Maybe user has no permissions.");
    }
  }
  aseq::~aseq(){
    snd_seq_close(seq);
  }


  std::vector<std::string> get_outputs(aseq *seq){
      std::vector<std::string> ret;

      snd_seq_client_info_t *cinfo;
      snd_seq_port_info_t *pinfo;
      int count;

      snd_seq_client_info_alloca(&cinfo);
      snd_seq_port_info_alloca(&pinfo);
      snd_seq_client_info_set_client(cinfo, -1);
      // DEBUG("Looking for outputs");

      while(snd_seq_query_next_client(seq->seq, cinfo) >= 0){
        // DEBUG("Test if client {}", snd_seq_client_info_get_name(cinfo));
        snd_seq_port_info_set_client(pinfo, snd_seq_client_info_get_client(cinfo));
        snd_seq_port_info_set_port(pinfo, -1);
        count = 0;
        while (snd_seq_query_next_port(seq->seq, pinfo) >= 0) {
          // DEBUG("Test if port {}:{}", snd_seq_client_info_get_name(cinfo), snd_seq_port_info_get_name(pinfo));
          if (!(snd_seq_port_info_get_capability(pinfo) & SND_SEQ_PORT_CAP_NO_EXPORT)){
            auto name = fmt::format(
                "{}:{}",
                snd_seq_client_info_get_name(cinfo),
                snd_seq_port_info_get_name(pinfo)
            );
            ret.push_back( std::move(name) );
            count++;
          }
        }
      }

      return ret;
  }
}

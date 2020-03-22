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
#include "./logger.hpp"
#include "./aseq.hpp"
#include "./exceptions.hpp"
#include "./poller.hpp"
#include "./rtpclient.hpp"
#include <alsa/seq.h>
#include <fmt/format.h>
#include <stdio.h>

namespace rtpmidid{
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

  aseq::aseq(std::string _name) : name(std::move(_name)){
    snd_lib_error_set_handler(error_handler);
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0){
      throw rtpmidid::exception("Can't open sequencer. Maybe user has no permissions.");
    }
    snd_seq_set_client_name(seq, name.c_str());
    snd_seq_nonblock(seq, 1);

    snd_seq_client_info_t *info;
    snd_seq_client_info_malloc(&info);
    snd_seq_get_client_info(seq, info);
    client_id = snd_seq_client_info_get_client(info);
    snd_seq_client_info_free(info);

    auto poller_count = snd_seq_poll_descriptors_count(seq, POLLIN);
    auto pfds = std::make_unique<struct pollfd[]>(poller_count);
    auto poller_count_check = snd_seq_poll_descriptors(seq, pfds.get(), poller_count, POLLIN);
    if (poller_count != poller_count_check){
      throw exception("ALSA seq poller count does not match. {} != {}", poller_count, poller_count_check);
    }
    // DEBUG("Got {} pollers", poller_count);
    for (int i=0;i<poller_count;i++){
      fds.push_back(pfds[i].fd);
      // DEBUG("Adding fd {} as alsa seq", pfds[i].fd);
      poller.add_fd_in(pfds[i].fd, [this](int){
        // INFO("New event at alsa seq");
        this->read_ready();
      });
    }
    read_ready();
  }

  aseq::~aseq(){
    for (auto fd: fds){
      poller.remove_fd(fd);
    }
    snd_seq_close(seq);
    snd_config_update_free_global();
  }

  /**
   * @short data is ready at the sequencer to read
   *
   * FUTURE OPTIMIZATION: Instead of sending events one by one, send them in
   *                      groups that go to the same port. This will save some
   *                      bandwidth.
   */
  void aseq::read_ready(){
    snd_seq_event_t *ev;
    int pending;
    while ( (pending = snd_seq_event_input(seq, &ev)) > 0 ){
      // DEBUG("ALSA MIDI event: {}, pending: {} / {}", ev->type, pending, snd_seq_event_input_pending(seq, 0));

      switch(ev->type){
        case SND_SEQ_EVENT_PORT_SUBSCRIBED:{
          // auto client = std::make_shared<rtpmidid::rtpclient>(name);
          uint8_t client, port;
          std::string name;
          snd_seq_addr_t *addr;
          if (ev->data.connect.sender.client != client_id){
            addr = &ev->data.connect.sender;
          } else {
            addr = &ev->data.connect.dest;
          }

          name = get_client_name(addr);
          client = addr->client;
          port = addr->port;
          auto myport = ev->dest.port;
          INFO("New ALSA connection from port {} ({}:{})", name, client, port);

          subscribe_event[myport](port_t(client, port), name);
        }
        break;
        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:{
          auto addr = &ev->data.addr;
          auto myport = ev->dest.port;
          unsubscribe_event[myport](port_t(addr->client, addr->port));
          DEBUG("Disconnected");
        }
        break;
        //case SND_SEQ_EVENT_NOTE:
        case SND_SEQ_EVENT_NOTEOFF:
        case SND_SEQ_EVENT_NOTEON:
        case SND_SEQ_EVENT_KEYPRESS:
        case SND_SEQ_EVENT_CONTROLLER:
        case SND_SEQ_EVENT_PGMCHANGE:
        case SND_SEQ_EVENT_CHANPRESS:
        case SND_SEQ_EVENT_PITCHBEND:
        case SND_SEQ_EVENT_SYSEX:
        case SND_SEQ_EVENT_SENSING:
        {
          auto myport = ev->dest.port;
          auto me = midi_event.find(myport);
          if (me != midi_event.end())
            me->second(ev);
        }
        break;
        default:
        static bool warning_raised[SND_SEQ_EVENT_NONE+1];
        if(!warning_raised[ev->type]) {
          warning_raised[ev->type]=true;
          WARNING("This event type {} is not managed yet", ev->type);
        }
        break;
      }

    }
  }

  uint8_t aseq::create_port(const std::string &name){
    auto caps = SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE|SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ;
    auto type = SND_SEQ_TYPE_INET;

    auto port = snd_seq_create_simple_port(seq, name.c_str(), caps, type);

    return port;
  }

  void aseq::remove_port(uint8_t port){
    snd_seq_delete_port(seq, port);
  }


  std::vector<std::string> get_ports(aseq *seq){
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

  std::string aseq::get_client_name(snd_seq_addr_t *addr){
    snd_seq_client_info_t *client_info = nullptr;
    snd_seq_client_info_malloc(&client_info);
    snd_seq_get_any_client_info(seq, addr->client, client_info);
    std::string client_name = snd_seq_client_info_get_name(client_info);

    snd_seq_port_info_t *port_info = nullptr;
    snd_seq_port_info_malloc(&port_info);
    snd_seq_get_any_port_info(seq, addr->client, addr->port, port_info);
    std::string port_name = snd_seq_port_info_get_name(port_info);

    snd_seq_client_info_free(client_info);
    snd_seq_port_info_free(port_info);

    //  Many times the name is just a copy
    if (client_name == port_name)
      return client_name;
    return fmt::format("{}-{}", client_name, port_name);
  }

  static void disconnect_port_at_subs(snd_seq_t *seq, snd_seq_query_subscribe_t *subs, uint8_t port) {
  	snd_seq_port_subscribe_t *port_sub;
    snd_seq_port_subscribe_alloca(&port_sub);

    for (auto type: {SND_SEQ_QUERY_SUBS_READ, SND_SEQ_QUERY_SUBS_WRITE}){
      snd_seq_query_subscribe_set_type(subs, type);
      snd_seq_query_subscribe_set_index(subs, 0);
      while (snd_seq_query_port_subscribers(seq, subs) >= 0) {
        const snd_seq_addr_t *addr;
        const snd_seq_addr_t *root;
        if (snd_seq_query_subscribe_get_type(subs) == SND_SEQ_QUERY_SUBS_READ){
          addr = snd_seq_query_subscribe_get_addr(subs);
          root = snd_seq_query_subscribe_get_root(subs);
        } else {
          root = snd_seq_query_subscribe_get_addr(subs);
          addr = snd_seq_query_subscribe_get_root(subs);
        }

        DEBUG("Disconnect {}:{} -> {}:{}", root->client, root->port, addr->client, addr->port);

        snd_seq_port_subscribe_set_sender(port_sub, root);
        snd_seq_port_subscribe_set_dest(port_sub, addr);
        if (snd_seq_unsubscribe_port(seq, port_sub) < 0) {
          ERROR("Could not disconenct ALSA seq ports: {}:{} -> {}:{}", root->client, root->port, addr->client, addr->port);
        }

        snd_seq_query_subscribe_set_index(subs, snd_seq_query_subscribe_get_index(subs) + 1);
      }
    }
  }

  void aseq::disconnect_port(uint8_t port){
    DEBUG("Disconnect alsa port {}", port);
    snd_seq_query_subscribe_t *subs;
    snd_seq_port_info_t *portinfo;

    snd_seq_port_info_alloca(&portinfo);
    if (snd_seq_get_port_info(seq, port, portinfo) < 0){
      throw rtpmidid::exception("Error getting port info");
    }

    snd_seq_query_subscribe_alloca(&subs);
    snd_seq_query_subscribe_set_root(subs, snd_seq_port_info_get_addr(portinfo));

    disconnect_port_at_subs(seq, subs, port);
  }
}

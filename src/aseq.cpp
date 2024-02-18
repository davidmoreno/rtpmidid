/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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
#include "./aseq.hpp"
#include <rtpmidid/logger.hpp>

namespace rtpmididns {
void error_handler(const char *file, int line, const char *function, int err,
                   const char *fmt, ...) {
  // NOLINTNEXTLINE
  va_list arg;
  std::string msg;
  // NOLINTNEXTLINE
  char buffer[1024];

  if (err == ENOENT) /* Ignore those misleading "warnings" */
    return;
  // NOLINTNEXTLINE
  va_start(arg, fmt);
  // NOLINTNEXTLINE
  vsprintf(buffer, fmt, arg);
  // NOLINTNEXTLINE
  msg += buffer;
  if (err) {
    msg += ": ";
    msg += snd_strerror(err);
  }
  // NOLINTNEXTLINE
  va_end(arg);
  std::string filename = "alsa/";
  filename += file;

  logger::__logger.log(filename.c_str(), line, ::logger::LogLevel::ERROR,
                       msg.c_str());
}

snd_seq_addr_t *get_other_ev_client_port(snd_seq_event_t *ev,
                                         uint8_t client_id) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  auto &connect = ev->data.connect;
  if (connect.sender.client != client_id) {
    return &connect.sender;
  } else {
    return &connect.dest;
  }
}
snd_seq_addr_t *get_my_ev_client_port(snd_seq_event_t *ev, uint8_t client_id) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  auto &connect = ev->data.connect;
  if (connect.sender.client == client_id) {
    return &connect.sender;
  } else {
    return &connect.dest;
  }
}
aseq_t::aseq_t(std::string _name) : name(std::move(_name)), seq(nullptr) {
  snd_lib_error_set_handler(error_handler);
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    throw alsa_connect_exception(
        "Can't open sequencer. Maybe user has no permissions.");
  }
  snd_seq_set_client_name(seq, name.c_str());
  snd_seq_nonblock(seq, 1);

  snd_seq_client_info_t *info = nullptr;
  snd_seq_client_info_malloc(&info);
  snd_seq_get_client_info(seq, info);
  client_id = snd_seq_client_info_get_client(info);
  snd_seq_client_info_free(info);

  auto poller_count = snd_seq_poll_descriptors_count(seq, POLLIN);
  // NOLINTNEXTLINE
  auto pfds = std::make_unique<struct pollfd[]>(poller_count);
  auto poller_count_check =
      snd_seq_poll_descriptors(seq, pfds.get(), poller_count, POLLIN);
  if (poller_count != poller_count_check) {
    throw rtpmidid::exception("ALSA seq poller count does not match. {} != {}",
                              poller_count, poller_count_check);
  }
  // DEBUG("Got {} pollers", poller_count);
  for (int i = 0; i < poller_count; i++) {
    // fds.push_back(pfds[i].fd);
    // DEBUG("Adding fd {} as alsa seq", pfds[i].fd);
    aseq_listener.emplace_back(
        rtpmidid::poller.add_fd_in(pfds[i].fd, [this](int) {
          // INFO("New event at alsa seq");
          this->read_ready();
        }));
  }
  read_ready();
}

aseq_t::~aseq_t() {
  // for (auto fd : fds) {
  //   try {
  //     poller.remove_fd(fd);
  //   } catch (rtpmidid::exception &e) {
  //     ERROR("Error removing aseq socket: {}", e.what());
  //   }
  // }
  aseq_listener.clear();
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
void aseq_t::read_ready() {
  snd_seq_event_t *ev = nullptr;
  while (snd_seq_event_input(seq, &ev) > 0) {
    // DEBUG("ALSA MIDI event: {}, pending: {} / {}", ev->type, pending,
    // snd_seq_event_input_pending(seq, 0));

    switch (ev->type) {
    case SND_SEQ_EVENT_PORT_SUBSCRIBED: {
      // auto client = std::make_shared<rtpmidid::rtpclient>(name);
      std::string name;
      snd_seq_addr_t *other = get_other_ev_client_port(ev, client_id);
      snd_seq_addr_t *me = get_my_ev_client_port(ev, client_id);

      INFO("Event source if {}:{}", ev->source.client, ev->source.port);

      name = get_client_name(other);

      INFO("New ALSA connection {} from port {}:{} -> {}:{}", name,
           other->client, other->port, me->client, me->port);

      if (other->client != client_id && me->client != client_id) {
        INFO("This connection is not to me. Ignore.");
        continue;
      }

      subscribe_event[me->port](port_t(other->client, other->port), name);
      if (me->client == other->client) {
        // This is an internal connection, should send subscribe from the other
        // side too
        name = get_client_name(me);
        INFO("New ALSA connection from port {} ({}:{}) (internal)", name,
             me->client, me->port);
        subscribe_event[other->port](port_t(me->client, me->port), name);
      }
    } break;
    case SND_SEQ_EVENT_PORT_UNSUBSCRIBED: {
      snd_seq_addr_t *other = get_other_ev_client_port(ev, client_id);
      snd_seq_addr_t *me = get_my_ev_client_port(ev, client_id);
      DEBUG("Disconnected {}:{} -> {}:{}", other->client, other->port,
            me->client, me->port);

      if (other->client != client_id && me->client != client_id) {
        INFO("This disconnection is not to me. Ignore.");
        continue;
      }

      unsubscribe_event[me->port](port_t(other->client, other->port));
      if (me->client == other->client) {
        // This is an internal connection, should send unsubscribe from the
        // other side too
        unsubscribe_event[other->port](port_t(me->client, me->port));
      }
    } break;
    // case SND_SEQ_EVENT_NOTE:
    case SND_SEQ_EVENT_CLOCK:
    case SND_SEQ_EVENT_START:
    case SND_SEQ_EVENT_CONTINUE:
    case SND_SEQ_EVENT_STOP:
    case SND_SEQ_EVENT_NOTEOFF:
    case SND_SEQ_EVENT_NOTEON:
    case SND_SEQ_EVENT_KEYPRESS:
    case SND_SEQ_EVENT_CONTROLLER:
    case SND_SEQ_EVENT_PGMCHANGE:
    case SND_SEQ_EVENT_CHANPRESS:
    case SND_SEQ_EVENT_PITCHBEND:
    case SND_SEQ_EVENT_SYSEX:
    case SND_SEQ_EVENT_QFRAME:
    case SND_SEQ_EVENT_SENSING: {
      auto myport = ev->dest.port;
      auto me = midi_event.find(myport);
      if (me != midi_event.end())
        me->second(ev);
    } break;
    case SND_SEQ_EVENT_PORT_START: {
      auto name = get_client_name(&ev->data.addr);
      auto type = get_client_type(&ev->data.addr);
      auto port = port_t(ev->data.addr.client, ev->data.addr.port);
      DEBUG("Client start {} {} {}", name, type, port);
      added_port_announcement(name, type, port);
    } break;
    case SND_SEQ_EVENT_PORT_EXIT: {
      auto port = port_t(ev->data.addr.client, ev->data.addr.port);
      DEBUG("Client exit {}", port);
      removed_port_announcement(port);
    } break;
    default:
      static std::array<bool, SND_SEQ_EVENT_NONE + 1> warning_raised{};
      // assert(ev->type < warning_raised.size());
      // NOLINTNEXTLINE
      if (!warning_raised[ev->type]) {

        // NOLINTNEXTLINE
        warning_raised[ev->type] = true;
        WARNING("This event type {} is not managed yet", ev->type);
      }
      break;
    }
  }
}

uint8_t aseq_t::create_port(const std::string &name, bool do_export) {
  auto caps = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE |
              SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
  auto type = SND_SEQ_TYPE_INET;

  if (!do_export) {
    caps |= SND_SEQ_PORT_CAP_NO_EXPORT;
  }

  auto port = snd_seq_create_simple_port(seq, name.c_str(), caps, type);

  return port;
}

void aseq_t::remove_port(uint8_t port) {
  snd_seq_delete_port(seq, port);
  midi_event.erase(port);
}

std::vector<std::string> get_ports(aseq_t *seq) {
  std::vector<std::string> ret;

  snd_seq_client_info_t *cinfo = nullptr;
  snd_seq_port_info_t *pinfo = nullptr;

  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);
  snd_seq_client_info_set_client(cinfo, -1);
  // DEBUG("Looking for outputs");

  while (snd_seq_query_next_client(seq->seq, cinfo) >= 0) {
    [[maybe_unused]] int count = 0;

    // DEBUG("Test if client {}", snd_seq_client_info_get_name(cinfo));
    snd_seq_port_info_set_client(pinfo, snd_seq_client_info_get_client(cinfo));
    snd_seq_port_info_set_port(pinfo, -1);
    while (snd_seq_query_next_port(seq->seq, pinfo) >= 0) {
      // DEBUG("Test if port {}:{}", snd_seq_client_info_get_name(cinfo),
      // snd_seq_port_info_get_name(pinfo));
      if (!(snd_seq_port_info_get_capability(pinfo) &
            SND_SEQ_PORT_CAP_NO_EXPORT)) {
        auto name = fmt::format("{}:{}", snd_seq_client_info_get_name(cinfo),
                                snd_seq_port_info_get_name(pinfo));
        ret.push_back(std::move(name));
        count++;
      }
    }
  }

  return ret;
}

std::string aseq_t::get_client_name(snd_seq_addr_t *addr) {
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

aseq_t::client_type_e get_type_by_seq_type(int type) {
  // Known types so far.. may be increased later? Dont know how to make it more
  // future proof. If change here, quite probably will be incompatible changes
  DEBUG("Type: {:b}", type);
  if (type & 0b01'00000000'00000000) {
    return aseq_t::client_type_e::TYPE_HARDWARE;
  } else if (type == 0x02) {
    return aseq_t::client_type_e::TYPE_HARDWARE;
  } else {
    return aseq_t::client_type_e::TYPE_SOFTWARE;
  }
}

aseq_t::client_type_e aseq_t::get_client_type(snd_seq_addr_t *addr) {
  // get client type using snd_seq_port_info_get_type(const snd_seq_port_info_t
  // *info);
  snd_seq_client_info_t *client_info = nullptr;
  snd_seq_client_info_malloc(&client_info);
  snd_seq_get_any_client_info(seq, addr->client, client_info);
  std::string client_name = snd_seq_client_info_get_name(client_info);

  snd_seq_port_info_t *port_info = nullptr;
  snd_seq_port_info_malloc(&port_info);
  snd_seq_get_any_port_info(seq, addr->client, addr->port, port_info);
  std::string port_name = snd_seq_port_info_get_name(port_info);

  auto type = snd_seq_client_info_get_type(client_info);
  // snd_seq_port_info_get_type(port_info);

  snd_seq_client_info_free(client_info);
  snd_seq_port_info_free(port_info);

  return get_type_by_seq_type(type);
}

static void disconnect_port_at_subs(snd_seq_t *seq,
                                    snd_seq_query_subscribe_t *subs,
                                    uint8_t port) {
  snd_seq_port_subscribe_t *port_sub = nullptr;
  snd_seq_port_subscribe_alloca(&port_sub);

  for (auto type : {SND_SEQ_QUERY_SUBS_READ, SND_SEQ_QUERY_SUBS_WRITE}) {
    snd_seq_query_subscribe_set_type(subs, type);
    snd_seq_query_subscribe_set_index(subs, 0);
    while (snd_seq_query_port_subscribers(seq, subs) >= 0) {
      const snd_seq_addr_t *addr = nullptr;
      const snd_seq_addr_t *root = nullptr;
      if (snd_seq_query_subscribe_get_type(subs) == SND_SEQ_QUERY_SUBS_READ) {
        addr = snd_seq_query_subscribe_get_addr(subs);
        root = snd_seq_query_subscribe_get_root(subs);
      } else {
        root = snd_seq_query_subscribe_get_addr(subs);
        addr = snd_seq_query_subscribe_get_root(subs);
      }

      DEBUG("Disconnect {}:{} -> {}:{}", root->client, root->port, addr->client,
            addr->port);

      snd_seq_port_subscribe_set_sender(port_sub, root);
      snd_seq_port_subscribe_set_dest(port_sub, addr);
      if (snd_seq_unsubscribe_port(seq, port_sub) < 0) {
        ERROR("Could not disconenct ALSA seq ports: {}:{} -> {}:{}",
              root->client, root->port, addr->client, addr->port);
      }

      snd_seq_query_subscribe_set_index(
          subs, snd_seq_query_subscribe_get_index(subs) + 1);
    }
  }
}

void aseq_t::disconnect_port(uint8_t port) {
  DEBUG("Disconnect alsa port {}", port);
  snd_seq_query_subscribe_t *subs = nullptr;
  snd_seq_port_info_t *portinfo = nullptr;

  snd_seq_port_info_alloca(&portinfo);
  if (snd_seq_get_port_info(seq, port, portinfo) < 0) {
    throw rtpmidid::exception("Error getting port info");
  }

  snd_seq_query_subscribe_alloca(&subs);
  snd_seq_query_subscribe_set_root(subs, snd_seq_port_info_get_addr(portinfo));

  disconnect_port_at_subs(seq, subs, port);
}

aseq_t::connection_t aseq_t::connect(const port_t &from, const port_t &to) {
  DEBUG("Connect alsa ports {} -> {}", from.to_string(), to.to_string());

  if (from.client == client_id) {
    int res = snd_seq_connect_to(seq, from.port, to.client, to.port);
    if (res == -16) {
      WARNING("ALSA seq error 16: {} -> {}. Already connected?",
              from.to_string(), to.to_string());
      return aseq_t::connection_t(shared_from_this(), from, to);
    }
    if (res < 0) {
      throw rtpmidid::exception("Failed connection: {} -> {}: {} ({})",
                                from.to_string(), to.to_string(),
                                snd_strerror(res), res);
    }
  } else if (to.client == client_id) {
    int res = snd_seq_connect_from(seq, to.port, from.client, from.port);
    if (res == -16) {
      WARNING("ALSA seq error 16: {} -> {}. Already connected?",
              from.to_string(), to.to_string());
      return aseq_t::connection_t(shared_from_this(), from, to);
    }
    if (res < 0) {
      throw rtpmidid::exception("Failed connection: {} -> {}: {} ({})",
                                from.to_string(), to.to_string(),
                                snd_strerror(res), res);
    }
  } else {
    ERROR("Can not connect ports I'm not part of.");
    throw rtpmidid::exception("Can not connect ports I'm not part of.");
  }
  return aseq_t::connection_t(shared_from_this(), from, to);
}

void aseq_t::disconnect(const port_t &from, const port_t &to) {
  DEBUG("Disconnect alsa ports {} -> {}", from.to_string(), to.to_string());

  if (from.client == client_id) {
    int res = snd_seq_disconnect_to(seq, from.port, to.client, to.port);
    if (res < 0) {
      ERROR("Failed disconnection: {} -> {}: {} ({})", from.to_string(),
            to.to_string(), snd_strerror(res), res);
      // throw rtpmidid::exception("Failed disconnection: {} -> {}: {} ({})",
      //                           from.to_string(), to.to_string(),
      //                           snd_strerror(res), res);
    }
  } else if (to.client == client_id) {
    int res = snd_seq_disconnect_from(seq, to.port, from.client, from.port);
    if (res < 0) {
      ERROR("Failed disconnection: {} -> {}: {} ({})", from.to_string(),
            to.to_string(), snd_strerror(res), res);
      // throw rtpmidid::exception("Failed disconnection: {} -> {}: {} ({})",
      //                           from.to_string(), to.to_string(),
      //                           snd_strerror(res), res);
    }
  } else {
    ERROR("Can not disconnect ports I'm not part of.");
    throw rtpmidid::exception("Can not disconnect ports I'm not part of.");
  }
}

/// List all devices
uint8_t aseq_t::find_device(const std::string &name) {
  snd_seq_client_info_t *cinfo = nullptr;
  int retv = -1;

  int ret = snd_seq_client_info_malloc(&cinfo);
  if (ret != 0)
    throw rtpmidid::exception("Error allocating memory for alsa clients");

  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(seq, cinfo) >= 0) {
    int cid = snd_seq_client_info_get_client(cinfo);
    if (cid < 0) {
      throw rtpmidid::exception("Error getting client info");
    }
    const char *cname = snd_seq_client_info_get_name(cinfo);
    if (cname == name) {
      retv = cid;
    }
  }
  snd_seq_client_info_free(cinfo);

  if (retv < 0) {
    throw rtpmidid::exception("Device not found");
  }

  return retv;
}

/// List all ports of a device
uint8_t aseq_t::find_port(uint8_t device_id, const std::string &name) {
  int retv = 0;
  snd_seq_port_info_t *pinfo = nullptr;

  int ret = snd_seq_port_info_malloc(&pinfo);
  if (ret != 0)
    throw rtpmidid::exception("Error allocating memory for alsa ports");

  snd_seq_port_info_set_client(pinfo, device_id);

  snd_seq_port_info_set_port(pinfo, -1);
  while (snd_seq_query_next_port(seq, pinfo) >= 0) {
    int pid = snd_seq_port_info_get_port(pinfo);
    if (pid < 0) {
      throw rtpmidid::exception("Error getting client info");
    }
    const char *pname = snd_seq_port_info_get_name(pinfo);

    if (pname == name) {
      retv = pid;
    }
  }

  snd_seq_port_info_free(pinfo);

  if (retv < 0) {
    throw rtpmidid::exception("Port not found");
  }

  return retv;
}

void aseq_t::for_devices(
    std::function<void(uint8_t, const std::string &, aseq_t::client_type_e)>
        func) {
  snd_seq_client_info_t *cinfo;

  int ret = snd_seq_client_info_malloc(&cinfo);
  if (ret != 0)
    throw rtpmidid::exception("Error allocating memory for alsa clients");

  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(seq, cinfo) >= 0) {
    int cid = snd_seq_client_info_get_client(cinfo);
    if (cid < 0) {
      throw rtpmidid::exception("Error getting client info");
    }
    auto cname = snd_seq_client_info_get_name(cinfo);
    auto type = get_type_by_seq_type(snd_seq_client_info_get_type(cinfo));
    // DEBUG("Client {} {} {:b}", cid, cname,
    // snd_seq_client_info_get_type(cinfo));

    func(cid, cname, type);
  }
  snd_seq_client_info_free(cinfo);
}

void aseq_t::for_ports(uint8_t device_id,
                       std::function<void(uint8_t, const std::string &)> func) {
  snd_seq_port_info_t *pinfo;

  int ret = snd_seq_port_info_malloc(&pinfo);
  if (ret != 0)
    throw rtpmidid::exception("Error allocating memory for alsa ports");

  snd_seq_port_info_set_client(pinfo, device_id);

  snd_seq_port_info_set_port(pinfo, -1);
  while (snd_seq_query_next_port(seq, pinfo) >= 0) {
    int pid = snd_seq_port_info_get_port(pinfo);
    if (pid < 0) {
      throw rtpmidid::exception("Error getting client info");
    }
    const char *pname = snd_seq_port_info_get_name(pinfo);

    func(pid, pname);
  }

  snd_seq_port_info_free(pinfo);
}

mididata_to_alsaevents_t::mididata_to_alsaevents_t() : buffer(nullptr) {
  snd_midi_event_new(1024, &buffer);
}
mididata_to_alsaevents_t::~mididata_to_alsaevents_t() {
  if (buffer)
    snd_midi_event_free(buffer);
}

void mididata_to_alsaevents_t::mididata_to_evs_f(
    rtpmidid::io_bytes_reader &data,
    std::function<void(snd_seq_event_t *)> func) {
  snd_seq_event_t ev;

  snd_midi_event_reset_encode(buffer);

  while (data.position < data.end) {
    // DEBUG("mididata to snd_ev, left {}", data);
    snd_seq_ev_clear(&ev);
    auto used = snd_midi_event_encode(buffer, data.position,
                                      data.end - data.position, &ev);
    if (used <= 0) {
      ERROR("Fail encode event: {}, {}", used, data);
      data.print_hex(false);
      return;
    }
    data.position += used;
    func(&ev);
  }
}

void mididata_to_alsaevents_t::ev_to_mididata(snd_seq_event_t *ev,
                                              rtpmidid::io_bytes_writer &data) {
  snd_midi_event_reset_decode(buffer);
  auto ret = snd_midi_event_decode(buffer, data.position,
                                   data.end - data.position, ev);
  if (ret < 0) {
    ERROR("Could not translate alsa seq event. Do nothing.");
    return;
  }

  data.position += ret;
  // DEBUG("ev to mididata, left: {}, {}", ret, data);
}

} // namespace rtpmididns

fmt::appender
fmt::formatter<rtpmididns::aseq_t::port_t>::format(rtpmididns::aseq_t::port_t c,
                                                   format_context &ctx) {
  auto name = fmt::format("port_t[{}, {}]", c.client, c.port);
  return formatter<std::string_view>::format(name, ctx);
}

fmt::appender fmt::formatter<rtpmididns::aseq_t::client_type_e>::format(
    rtpmididns::aseq_t::client_type_e c, format_context &ctx) {
  auto name = c == rtpmididns::aseq_t::client_type_e::TYPE_HARDWARE
                  ? "TYPE_HARDWARE"
                  : "TYPE_SOFTWARE";
  return formatter<std::string_view>::format(name, ctx);
}

fmt::appender fmt::formatter<rtpmididns::aseq_t::connection_t>::format(
    const rtpmididns::aseq_t::connection_t &c, format_context &ctx) {
  auto name =
      fmt::format("connection_t[{}, {} -> {}]", c.connected, c.from, c.to);
  return formatter<std::string_view>::format(name, ctx);
}

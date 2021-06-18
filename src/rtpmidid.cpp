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
#include <stdlib.h>
#include <string>

#include "./aseq.hpp"
#include "./config.hpp"
#include "./rtpmidid.hpp"
#include "./stringpp.hpp"
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/rtpclient.hpp>
#include <rtpmidid/rtpserver.hpp>

using namespace rtpmidid;
using namespace std::chrono_literals;

rtpmidid_t::rtpmidid_t(config_t *_config)
    : name(_config->name), seq(fmt::format("rtpmidi {}", name)),
      config(_config) {
  setup_mdns();
  setup_alsa_seq(config->export_name);

  for (auto &port : config->ports) {
    auto server = add_rtpmidid_import_server(config->name, port);
    servers.push_back(std::move(server));
  }

  for (auto &connect_to : config->connect_to) {
    auto res = add_rtpmidi_client(connect_to);
    if (res == std::nullopt) {
      throw rtpmidid::exception("Invalid address to connect to. Aborting.");
    }
  }
}

std::optional<uint8_t>
rtpmidid_t::add_rtpmidi_client(const std::string &connect_to) {
  INFO("Connecting to {}", connect_to);
  std::vector<std::string> s;
  auto find_sbracket = connect_to.find('[');
  if (find_sbracket != std::string::npos) {
    if (find_sbracket != 0)
      s.push_back(connect_to.substr(0, find_sbracket - 1));

    auto find_ebracket = connect_to.find(']');
    if (!find_ebracket) {
      ERROR("Error on address. For IPV6 Address, use name:[ipv6]:port. {}",
            connect_to);
      return std::nullopt;
    }
    s.push_back(connect_to.substr(find_sbracket + 1,
                                  find_ebracket - find_sbracket - 1));

    if (find_ebracket + 2 < connect_to.size())
      s.push_back(connect_to.substr(find_ebracket + 2, std::string::npos));
  } else {
    s = ::rtpmidid::split(connect_to, ':');
  }

  if (s.size() == 1) {
    return add_rtpmidi_client(s[0], s[0], "5004");
  } else if (s.size() == 2) {
    return add_rtpmidi_client(s[0], s[0], s[1].c_str());
  } else if (s.size() == 3) {
    return add_rtpmidi_client(s[0], s[1], s[2].c_str());
  } else {
    ERROR("Invalid remote address. Format is host, name:host, or "
          "name:host:port. Host can be a hostname, ip4 address, or [ip6] "
          "address (ip6:[::1]:5004). {}",
          s.size());
    return std::nullopt;
  }
}

void rtpmidid_t::announce_rtpmidid_server(const std::string &name,
                                          uint16_t port) {
  mdns_rtpmidi.announce_rtpmidi(name, port);
}

void rtpmidid_t::unannounce_rtpmidid_server(const std::string &name,
                                            uint16_t port) {
  mdns_rtpmidi.unannounce_rtpmidi(name, port);
}

std::shared_ptr<rtpserver>
rtpmidid_t::add_rtpmidid_import_server(const std::string &name,
                                       const std::string &port) {
  auto rtpserver = std::make_shared<::rtpmidid::rtpserver>(name, port);

  announce_rtpmidid_server(name, rtpserver->control_port);

  auto wrtpserver = std::weak_ptr(rtpserver);
  rtpserver->connected_event.connect(
      [this, wrtpserver, port](std::shared_ptr<::rtpmidid::rtppeer> peer) {
        if (wrtpserver.expired()) {
          return;
        }
        auto rtpserver = wrtpserver.lock();

        INFO("Remote client connects to local server at port {}. Name: {}",
             port, peer->remote_name);
        auto aseq_port = seq.create_port(peer->remote_name);

        peer->midi_event.connect([this, aseq_port](io_bytes_reader pb) {
          this->recv_rtpmidi_event(aseq_port, pb);
        });
        seq.midi_event[aseq_port].connect(
            [this, aseq_port](snd_seq_event_t *ev) {
              auto peer_it = known_servers_connections.find(aseq_port);
              if (peer_it == std::end(known_servers_connections)) {
                WARNING("Got MIDI event in an non existing anymore peer.");
                return;
              }
              auto conn = &peer_it->second;

              io_bytes_writer_static<4096> stream;
              alsamidi_to_midiprotocol(ev, stream);
              conn->peer->send_midi(stream);
            });
        peer->disconnect_event.connect([this, aseq_port](auto reason) {
          DEBUG("Remove aseq port {}", aseq_port);
          seq.remove_port(aseq_port);
          known_servers_connections.erase(aseq_port);
        });

        server_conn_info server_conn = {
            peer->remote_name,
            peer,
            rtpserver,
        };

        known_servers_connections[aseq_port] = server_conn;
      });

  return rtpserver;
}

std::shared_ptr<rtpserver>
rtpmidid_t::add_rtpmidid_export_server(const std::string &name,
                                       uint8_t alsaport, aseq::port_t &from) {

  for (auto &alsa_server : alsa_to_server) {
    auto server = alsa_server.second;
    if (server->name == name) {
      INFO("Already a rtpserver for this ALSA name at {}:{} / {}. RTPMidi "
           "port: {}",
           from.client, from.port, name, server->control_port);
      return server;
    }
  }

  char port_name[16];
  snprintf(port_name, sizeof(port_name), "%d", config->client_port);

  auto server = std::make_shared<rtpserver>(name, port_name);

  announce_rtpmidid_server(name, server->control_port);

  seq.midi_event[alsaport].connect([this, server](snd_seq_event_t *ev) {
    io_bytes_writer_static<4096> buffer;
    alsamidi_to_midiprotocol(ev, buffer);
    server->send_midi_to_all_peers(buffer);
  });

  seq.unsubscribe_event[alsaport].connect(
      [this, name, server](aseq::port_t from) {
        // This should destroy the server.
        unannounce_rtpmidid_server(name, server->control_port);
        // TODO: disconnect from on_midi_event.
        alsa_to_server.erase(from);
      });

  server->midi_event.connect([this, alsaport](io_bytes_reader buffer) {
    this->recv_rtpmidi_event(alsaport, buffer);
  });

  alsa_to_server[from] = server;

  return server;
}

void rtpmidid_t::setup_alsa_seq(const std::string &name) {
  // Export only one, but all data that is conencted to it.
  // add_export_port();
  auto alsaport = seq.create_port(name);
  seq.subscribe_event[alsaport].connect(
      [this, alsaport](aseq::port_t from, const std::string &name) {
        DEBUG("Connected to ALSA port {}:{}. Create network server for this "
              "alsa data.",
              from.client, from.port);

        add_rtpmidid_export_server(fmt::format("{}/{}", this->name, name),
                                   alsaport, from);
      });
}

void rtpmidid_t::setup_mdns() {
  mdns_rtpmidi.discover_event.connect([this](const std::string &name,
                                             const std::string &address,
                                             const std::string &port) {
    this->add_rtpmidi_client(name, address, port);
  });

  mdns_rtpmidi.remove_event.connect([this](const std::string &name) {
    // TODO : remove client / alsa sessions
    this->remove_rtpmidi_client(name);
  });
}

std::optional<uint8_t>
rtpmidid_t::add_rtpmidi_client(const std::string &name,
                               const std::string &address,
                               const std::string &net_port) {
  for (auto &known : known_clients) {
    if (known.second.name == name) {
      // DEBUG(
      //     "Trying to add again rtpmidi {}:{} server. Quite probably mDNS re"
      //     "announce. " "Maybe somebody ask, or just periodically.", address,
      //     net_port
      // );
      known.second.addresses.push_back({address, net_port});
      return std::nullopt;
    }
  }

  uint8_t aseq_port = seq.create_port(name);
  auto peer_info = ::rtpmidid::client_info{
      name, {{address, net_port}}, 0, 0, nullptr, aseq_port,
  };

  INFO("New alsa port: {}, connects to host: {}, port: {}, name: {}", aseq_port,
       address, net_port, name);
  known_clients[aseq_port] = std::move(peer_info);

  seq.subscribe_event[aseq_port].connect(
      [this, aseq_port](aseq::port_t port, const std::string &name) {
        DEBUG("Callback on subscribe at rtpmidid: {}", name);
        connect_client(fmt::format("{}/{}", this->name, name), aseq_port);
      });
  seq.unsubscribe_event[aseq_port].connect(
      [this, aseq_port](aseq::port_t port) {
        DEBUG("Callback on unsubscribe at rtpmidid");
        auto peer_info = &known_clients[aseq_port];
        peer_info->use_count--;
        if (peer_info->use_count <= 0) {
          peer_info->peer = nullptr;
        }
      });
  seq.midi_event[aseq_port].connect([this, aseq_port](snd_seq_event_t *ev) {
    this->recv_alsamidi_event(aseq_port, ev);
  });

  return aseq_port;
}

void rtpmidid_t::remove_rtpmidi_client(const std::string &name) {
  INFO("Removing rtp midi client {}", name);

  for (auto I = known_clients.begin(), endI = known_clients.end(); I != endI;
       ++I) {
    if ((*I).second.name == name) {
      auto &known = *I;
      DEBUG("Found client to delete: alsa port {}. Deletes all known addreses.",
            known.first);
      remove_client(known.first);
      return;
    }
  }
  // WARNING("Service is not currently known to delete: {}", name);
}

void rtpmidid_t::connect_client(const std::string &name, int aseq_port) {
  auto peer_info = &known_clients[aseq_port];
  if (peer_info->peer) {
    if (peer_info->peer->peer.status == rtppeer::CONNECTED) {
      DEBUG("Already connected.");
    } else {
      DEBUG("Already connecting.");
    }
  } else {
    auto &address = peer_info->addresses[peer_info->addr_idx];
    peer_info->peer = std::make_shared<rtpclient>(name);
    peer_info->peer->peer.midi_event.connect(
        [this, aseq_port](io_bytes_reader pb) {
          this->recv_rtpmidi_event(aseq_port, pb);
        });
    peer_info->peer->peer.disconnect_event.connect(
        [this, aseq_port](rtppeer::disconnect_reason_e reason) {
          this->disconnect_client(aseq_port, reason);
        });
    peer_info->use_count++;

    peer_info->peer->connect_to(address.address, address.port,
                                config->client_port);
  }
}

void rtpmidid_t::disconnect_client(int aseq_port, int reasoni) {
  auto peer_info = &known_clients[aseq_port];
  auto reason = static_cast<rtppeer::disconnect_reason_e>(reasoni);

  DEBUG("Disconnect signal: {}", reason);
  // If cant connec t(network problem) or rejected, try again in next
  // address.
  switch (reason) {
  case rtppeer::disconnect_reason_e::CANT_CONNECT:
  case rtppeer::disconnect_reason_e::CONNECTION_REJECTED:
    if (peer_info->connect_attempts >= (8 * peer_info->addresses.size())) {
      ERROR("Too many attempts to connect. Not trying again. Attempted "
            "{} times.",
            peer_info->connect_attempts);
      remove_client(peer_info->aseq_port);
      return;
    }

    peer_info->peer->reset();

    peer_info->connect_attempts += 1;
    peer_info->peer->connect_timer = poller.add_timer_event(2s, [peer_info] {
      peer_info->addr_idx =
          (peer_info->addr_idx + 1) % peer_info->addresses.size();
      DEBUG("Try connect next in list. Idx {}/{}", peer_info->addr_idx,
            peer_info->addresses.size());
      // Try next
      auto &address = peer_info->addresses[peer_info->addr_idx];
      peer_info->peer->connect_to(address.address, address.port);
    });
    break;

  case rtppeer::disconnect_reason_e::CONNECT_TIMEOUT:
  case rtppeer::disconnect_reason_e::CK_TIMEOUT:
    WARNING("Timeout. Not trying again.");
    remove_client(peer_info->aseq_port);
    return;
    break;

  case rtppeer::disconnect_reason_e::PEER_DISCONNECTED:
    WARNING("Peer disconnected. Aseq disconnect.");
    seq.disconnect_port(peer_info->aseq_port);
    if (peer_info->use_count > 0)
      peer_info->use_count--;
    // Delete it, but later as we are here because of a call inside the peer
    if (peer_info->use_count == 0) {
      poller.call_later([this, aseq_port] {
        auto peer_info = &known_clients[aseq_port];
        if (peer_info)
          peer_info->peer = nullptr;
      });
    }
    // peer_info->peer = nullptr;
    // peer_info->use_count = 0;
    // remove_client(peer_info->aseq_port);
    break;

  case rtppeer::disconnect_reason_e::DISCONNECT:
    // Do nothing, another client may connect
    break;

  default:
    ERROR("Other reason: {}", reason);
    remove_client(peer_info->aseq_port);
  }
}

void rtpmidid_t::recv_rtpmidi_event(int port, io_bytes_reader &midi_data) {
  uint8_t current_command = 0;
  snd_seq_event_t ev;

  while (midi_data.position < midi_data.end) {
    // MIDI may reuse the last command if appropiate. For example several
    // consecutive Note On
    int maybe_next_command = midi_data.read_uint8();
    if (maybe_next_command & 0x80) {
      current_command = maybe_next_command;
    } else {
      midi_data.position--;
    }
    auto type = current_command & 0xF0;

    switch (type) {
    case 0xB0: // CC
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_controller(&ev, current_command & 0x0F,
                                midi_data.read_uint8(), midi_data.read_uint8());
      break;
    case 0x90:
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_noteon(&ev, current_command & 0x0F, midi_data.read_uint8(),
                            midi_data.read_uint8());
      break;
    case 0x80:
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_noteoff(&ev, current_command & 0x0F,
                             midi_data.read_uint8(), midi_data.read_uint8());
      break;
    case 0xA0:
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_keypress(&ev, current_command & 0x0F,
                              midi_data.read_uint8(), midi_data.read_uint8());
      break;
    case 0xC0:
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_pgmchange(&ev, current_command & 0x0F,
                               midi_data.read_uint8());
      break;
    case 0xD0:
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_chanpress(&ev, current_command & 0x0F,
                               midi_data.read_uint8());
      break;
    case 0xE0: {
      snd_seq_ev_clear(&ev);
      auto lsb = midi_data.read_uint8();
      auto msb = midi_data.read_uint8();
      auto pitch_bend = ((msb << 7) + lsb) - 8192;
      // DEBUG("Pitch bend received {}", pitch_bend);
      snd_seq_ev_set_pitchbend(&ev, current_command & 0x0F, pitch_bend);
    } break;
    case 0xF0: {
      // System messages
      switch (current_command) {
      case 0xF0: { // SysEx event
        auto start = midi_data.pos() - 1;
        auto len = 2;
        try {
          while (midi_data.read_uint8() != 0xf7)
            len++;
        } catch (exception &e) {
          WARNING("Malformed SysEx message in buffer has no end byte");
          break;
        }
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_sysex(&ev, len, &midi_data.start[start]);
      } break;
      case 0xF1: // MTC Quarter Frame package
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_fixed(&ev);
        ev.data.control.value = midi_data.read_uint8();
        ev.type = SND_SEQ_EVENT_QFRAME;
        break;
      case 0xFE: // Active sense
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_fixed(&ev);
        ev.type = SND_SEQ_EVENT_SENSING;
        break;
      case 0xF8: // Clock
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_fixed(&ev);
        ev.type = SND_SEQ_EVENT_CLOCK;
        break;
      case 0xFA: // start
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_fixed(&ev);
        ev.type = SND_SEQ_EVENT_START;
        break;
      case 0xFC: // stop
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_fixed(&ev);
        ev.type = SND_SEQ_EVENT_STOP;
        break;
      case 0xFB: // continue
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_fixed(&ev);
        ev.type = SND_SEQ_EVENT_CONTINUE;
        break;
      default:
        break;
      }
    } break;
    default:
      WARNING("MIDI command type {:02X} not implemented yet", type);
      return;
      break;
    }
    snd_seq_ev_set_source(&ev, port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_event_output_direct(seq.seq, &ev);
    // There is one delta time byte following, if there are multiple commands in
    // one frame. We ignore this
    if (midi_data.position < midi_data.end)
      midi_data.read_uint8();
    ;
  }
}

void rtpmidid_t::recv_alsamidi_event(int aseq_port, snd_seq_event *ev) {
  // DEBUG("Callback on midi event at rtpmidid, port {}", aseq_port);
  auto peer_info = &known_clients[aseq_port];
  if (!peer_info->peer) {
    ERROR("There is no peer but I received an event! This situation should "
          "NEVER happen. File a bug. Port {}",
          aseq_port);
    return;
  }

  io_bytes_writer_static<4096> stream;
  alsamidi_to_midiprotocol(ev, stream);
  peer_info->peer->peer.send_midi(stream);
}

void rtpmidid_t::alsamidi_to_midiprotocol(snd_seq_event_t *ev,
                                          io_bytes_writer &stream) {
  switch (ev->type) {
  // case SND_SEQ_EVENT_NOTE:
  case SND_SEQ_EVENT_NOTEON:
    stream.write_uint8(0x90 | (ev->data.note.channel & 0x0F));
    stream.write_uint8(ev->data.note.note);
    stream.write_uint8(ev->data.note.velocity);
    break;
  case SND_SEQ_EVENT_NOTEOFF:
    stream.write_uint8(0x80 | (ev->data.note.channel & 0x0F));
    stream.write_uint8(ev->data.note.note);
    stream.write_uint8(ev->data.note.velocity);
    break;
  case SND_SEQ_EVENT_KEYPRESS:
    stream.write_uint8(0xA0 | (ev->data.note.channel & 0x0F));
    stream.write_uint8(ev->data.note.note);
    stream.write_uint8(ev->data.note.velocity);
    break;
  case SND_SEQ_EVENT_CONTROLLER:
    stream.write_uint8(0xB0 | (ev->data.control.channel & 0x0F));
    stream.write_uint8(ev->data.control.param);
    stream.write_uint8(ev->data.control.value);
    break;
  case SND_SEQ_EVENT_PGMCHANGE:
    stream.write_uint8(0xC0 | (ev->data.control.channel));
    stream.write_uint8(ev->data.control.value & 0x0FF);
    break;
  case SND_SEQ_EVENT_CHANPRESS:
    stream.write_uint8(0xD0 | (ev->data.control.channel));
    stream.write_uint8(ev->data.control.value & 0x0FF);
    break;
  case SND_SEQ_EVENT_PITCHBEND:
    // DEBUG("Send pitch bend {}", ev->data.control.value);
    stream.write_uint8(0xE0 | (ev->data.control.channel & 0x0F));
    stream.write_uint8((ev->data.control.value + 8192) & 0x07F);
    stream.write_uint8((ev->data.control.value + 8192) >> 7 & 0x07F);
    break;
  case SND_SEQ_EVENT_SENSING:
    stream.write_uint8(0xFE);
    break;
  case SND_SEQ_EVENT_STOP:
    stream.write_uint8(0xFC);
    break;
  case SND_SEQ_EVENT_CLOCK:
    stream.write_uint8(0xF8);
    break;
  case SND_SEQ_EVENT_START:
    stream.write_uint8(0xFA);
    break;
  case SND_SEQ_EVENT_CONTINUE:
    stream.write_uint8(0xFB);
    break;
  case SND_SEQ_EVENT_SYSEX: {
    ssize_t len = ev->data.ext.len, sz = stream.size();
    if (len <= sz) {
      uint8_t *data = (unsigned char *)ev->data.ext.ptr;
      for (ssize_t i = 0; i < len; i++) {
        stream.write_uint8(data[i]);
      }
    } else {
      WARNING("Sysex buffer overflow! Not sending. ({} bytes needed)", len);
    }
  } break;
  case SND_SEQ_EVENT_QFRAME:
    stream.write_uint8(0xF1);
    stream.write_uint8(ev->data.control.value & 0x0FF);
    break;
  default:
    WARNING("Event type not yet implemented! Not sending. {}", ev->type);
    return;
    break;
  }
}

void rtpmidid_t::remove_client(uint8_t port) {
  // We add it to the poller queue as as GC, as the peer
  // might be further used at this call point.
  poller.call_later([this, port] {
    if (known_clients.find(port) == known_clients.end()) {
      DEBUG("Removing peer already removed from known peers list. Port {}",
            port);
      return;
    }
    DEBUG("Removing peer from known peers list. Port {}", port);
    seq.remove_port(port);
    seq.subscribe_event[port].disconnect_all();
    seq.unsubscribe_event[port].disconnect_all();
    seq.midi_event[port].disconnect_all();

    // Last as may be used in the shutdown of the client.
    known_clients.erase(port);
  });
}

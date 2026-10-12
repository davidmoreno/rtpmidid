/**
 * rtpmidid-lite: one ALSA sequencer port exported over RTP-MIDI.
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include "daemon.hpp"

#include <chrono>
#include <rtpmidid/logger.hpp>
#include <utility>

namespace rtpmidid_lite {

namespace {
/// How often to check whether avahi is ready to publish our service.
constexpr auto ANNOUNCE_RETRY_PERIOD = std::chrono::milliseconds(250);
/// Give avahi about ten seconds before giving up on the announcement.
constexpr int ANNOUNCE_MAX_ATTEMPTS = 40;
} // namespace

daemon_t::daemon_t(config_t config) : config_(std::move(config)) {}

daemon_t::~daemon_t() = default;

void daemon_t::start() {
  start_alsa();
  start_server();
  start_clients();
  if (config_.mdns)
    start_mdns();
}

void daemon_t::start_alsa() {
  alsa_ = std::make_unique<alsa_seq_t>(config_.name);

  alsa_midi_connection_ =
      alsa_->midi_event.connect([this](const rtpmidid::io_bytes_reader &data) {
        if (server_)
          server_->send_midi_to_all_peers(data);
        for (auto &client : clients_)
          client->client->peer.send_midi(data);
      });
}

void daemon_t::start_server() {
  server_ = std::make_unique<rtpmidid::rtpserver_t>(config_.name, config_.port);

  server_midi_connection_ = server_->midi_event.connect(
      [this](const rtpmidid::io_bytes_reader &data) {
        if (alsa_)
          alsa_->send_midi(data);
      });

  server_status_connection_ = server_->status_change_event.connect(
      [](const std::shared_ptr<rtpmidid::rtppeer_t> &peer,
         rtpmidid::rtppeer_t::status_e status) {
        if (status == rtpmidid::rtppeer_t::status_e::CONNECTED) {
          INFO("Peer '{}' connected from {}.", peer->remote_name,
               peer->remote_address.to_string());
        } else if (rtpmidid::rtppeer_t::is_disconnected(status)) {
          INFO("Peer '{}' disconnected ({}).", peer->remote_name, status);
        }
      });
}

void daemon_t::start_clients() {
  for (const auto &endpoint : config_.connect_to) {
    auto data = std::make_unique<client_t>();
    data->client = std::make_unique<rtpmidid::rtpclient_t>(config_.name);
    data->client->add_server_address(endpoint.host, endpoint.port);

    data->midi_connection = data->client->peer.midi_event.connect(
        [this](const rtpmidid::io_bytes_reader &midi) {
          if (alsa_)
            alsa_->send_midi(midi);
        });

    data->status_connection = data->client->peer.status_change_event.connect(
        [host = endpoint.host,
         port = endpoint.port](rtpmidid::rtppeer_t::status_e status) {
          if (status == rtpmidid::rtppeer_t::status_e::CONNECTED) {
            INFO("Connected to {}:{}.", host, port);
          } else if (rtpmidid::rtppeer_t::is_disconnected(status)) {
            INFO("Connection to {}:{} ended ({}).", host, port, status);
          }
        });

    INFO("Connecting to {}:{}...", endpoint.host, endpoint.port);
    data->client->connect();

    clients_.push_back(std::move(data));
  }
}

void daemon_t::start_mdns() {
  mdns_ = std::make_unique<rtpmidid::mdns_rtpmidi_t>();

  // avahi_client_new may have connected and created the entry group already, in
  // which case announce_rtpmidi publishes right away.
  const bool avahi_ready = mdns_->group != nullptr;

  const auto port = std::stoi(config_.port);
  mdns_->announce_rtpmidi(config_.name, port);

  if (avahi_ready)
    return;

  // Otherwise the announcement is queued: keep retrying until the entry group
  // exists (or give up after a while).
  announce_attempts_ = 0;
  try_announce();
}

void daemon_t::try_announce() {
  if (!mdns_)
    return;

  if (mdns_->group != nullptr) {
    mdns_->announce_all();
    return;
  }

  if (announce_attempts_++ >= ANNOUNCE_MAX_ATTEMPTS) {
    WARNING("Avahi was not ready, '{}' was not announced over mDNS.",
            config_.name);
    return;
  }

  announce_timer_ = rtpmidid::poller.add_timer_event(
      ANNOUNCE_RETRY_PERIOD, [this]() { try_announce(); });
}

} // namespace rtpmidid_lite

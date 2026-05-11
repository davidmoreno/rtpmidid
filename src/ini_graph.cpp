/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 */

#include "ini_graph.hpp"
#include "settings.hpp"
#include <rtpmidid/exceptions.hpp>
#include <unordered_set>

namespace rtpmididns {

static std::string param_or_empty(const settings_t::ini_peer_template_t &p,
                                  const std::string &k) {
  auto it = p.params.find(k);
  return it == p.params.end() ? std::string() : it->second;
}

static const settings_t::ini_peer_template_t *
find_peer_by_id(const std::vector<settings_t::ini_peer_template_t> &peers,
                const std::string &id) {
  for (const auto &p : peers) {
    if (p.id == id)
      return &p;
  }
  return nullptr;
}

static void remove_connects_involving(std::vector<settings_t::ini_connect_t> &conns,
                                      const std::string &id) {
  std::vector<settings_t::ini_connect_t> next;
  next.reserve(conns.size());
  for (const auto &c : conns) {
    if (c.from_id != id && c.to_id != id)
      next.push_back(c);
  }
  conns.swap(next);
}

static void remove_peer_id(std::vector<settings_t::ini_peer_template_t> &peers,
                           const std::string &id) {
  std::vector<settings_t::ini_peer_template_t> next;
  next.reserve(peers.size());
  for (const auto &p : peers) {
    if (p.id != id)
      next.push_back(p);
  }
  peers.swap(next);
}

static bool has_bidirectional_edge(const std::vector<settings_t::ini_connect_t> &conns,
                                   const std::string &a, const std::string &b) {
  bool ab = false;
  bool ba = false;
  for (const auto &c : conns) {
    if (c.from_id == a && c.to_id == b)
      ab = true;
    if (c.from_id == b && c.to_id == a)
      ba = true;
  }
  return ab && ba;
}

static bool has_any_edge_between(const std::vector<settings_t::ini_connect_t> &conns,
                                 const std::string &a, const std::string &b) {
  for (const auto &c : conns) {
    if ((c.from_id == a && c.to_id == b) || (c.from_id == b && c.to_id == a))
      return true;
  }
  return false;
}

void finalize_unified_ini_graph(settings_t &s, const std::string &ini_filename) {
  if (s.ini_peers.empty() && s.ini_connects.empty())
    return;

  std::unordered_set<std::string> seen_ids;
  for (const auto &p : s.ini_peers) {
    if (p.id.empty())
      throw rtpmidid::exception("INI [peer]: empty id in {}", ini_filename);
    if (!seen_ids.insert(p.id).second)
      throw rtpmidid::exception("INI [peer]: duplicate id '{}' in {}", p.id,
                                ini_filename);
  }

  auto has_peer_id = [&](const std::string &id) {
    return find_peer_by_id(s.ini_peers, id) != nullptr;
  };
  for (const auto &c : s.ini_connects) {
    if (!has_peer_id(c.from_id))
      throw rtpmidid::exception("INI [connect]: unknown from id '{}' in {}",
                                c.from_id, ini_filename);
    if (!has_peer_id(c.to_id))
      throw rtpmidid::exception("INI [connect]: unknown to id '{}' in {}",
                                c.to_id, ini_filename);
  }

  // Raw MIDI <-> RTP: two peers, two opposite directed edges
  while (true) {
    bool lowered = false;
    for (const auto &c : s.ini_connects) {
      const std::string a = c.from_id;
      const std::string b = c.to_id;
      if (a == b)
        continue;
      if (!has_bidirectional_edge(s.ini_connects, a, b))
        continue;
      const settings_t::ini_peer_template_t *pa = find_peer_by_id(s.ini_peers, a);
      const settings_t::ini_peer_template_t *pb = find_peer_by_id(s.ini_peers, b);
      if (!pa || !pb)
        continue;
      const settings_t::ini_peer_template_t *raw = nullptr;
      const settings_t::ini_peer_template_t *rtp = nullptr;
      if (pa->type == "rawmidi" &&
          (pb->type == "rtpmidi_listen" || pb->type == "rtpmidi_connect")) {
        raw = pa;
        rtp = pb;
      } else if (pb->type == "rawmidi" &&
                 (pa->type == "rtpmidi_listen" ||
                  pa->type == "rtpmidi_connect")) {
        raw = pb;
        rtp = pa;
      } else {
        continue;
      }

      settings_t::rawmidi_t rm;
      rm.device = param_or_empty(*raw, "device");
      rm.name = param_or_empty(*raw, "name");
      if (rm.device.empty())
        throw rtpmidid::exception(
            "INI peer '{}' type rawmidi: device is required in {}", raw->id,
            ini_filename);
      if (rtp->type == "rtpmidi_listen") {
        rm.hostname = "";
        rm.local_udp_port = param_or_empty(*rtp, "local_udp_port");
        rm.remote_udp_port = "";
      } else {
        rm.hostname = param_or_empty(*rtp, "hostname");
        rm.remote_udp_port = param_or_empty(*rtp, "port");
        if (rm.remote_udp_port.empty())
          rm.remote_udp_port = param_or_empty(*rtp, "remote_udp_port");
        rm.local_udp_port = param_or_empty(*rtp, "local_udp_port");
      }
      s.rawmidi.push_back(std::move(rm));
      remove_connects_involving(s.ini_connects, a);
      remove_connects_involving(s.ini_connects, b);
      remove_peer_id(s.ini_peers, a);
      remove_peer_id(s.ini_peers, b);
      lowered = true;
      break;
    }
    if (!lowered)
      break;
  }

  // ALSA listener + RTP client: Option A — single connect_to_t (any edge between)
  while (true) {
    bool lowered = false;
    for (size_t i = 0; i < s.ini_peers.size(); ++i) {
      for (size_t j = 0; j < s.ini_peers.size(); ++j) {
        if (i == j)
          continue;
        const settings_t::ini_peer_template_t &pa = s.ini_peers[i];
        const settings_t::ini_peer_template_t &pb = s.ini_peers[j];
        const settings_t::ini_peer_template_t *alsa = nullptr;
        const settings_t::ini_peer_template_t *rtp = nullptr;
        if (pa.type == "alsa_listener" && pb.type == "rtpmidi_connect") {
          alsa = &pa;
          rtp = &pb;
        } else if (pb.type == "alsa_listener" && pa.type == "rtpmidi_connect") {
          alsa = &pb;
          rtp = &pa;
        } else {
          continue;
        }
        if (!has_any_edge_between(s.ini_connects, alsa->id, rtp->id))
          continue;

        settings_t::connect_to_t ct;
        ct.name = param_or_empty(*alsa, "name");
        ct.hostname = param_or_empty(*rtp, "hostname");
        ct.port = param_or_empty(*rtp, "port");
        if (ct.port.empty())
          ct.port = param_or_empty(*rtp, "remote_udp_port");
        ct.local_udp_port = param_or_empty(*rtp, "local_udp_port");
        if (ct.name.empty() || ct.hostname.empty())
          throw rtpmidid::exception(
              "INI alsa_listener/rtpmidi_connect pair: name and remote "
              "hostname are required in {}",
              ini_filename);
        s.connect_to.push_back(std::move(ct));
        remove_connects_involving(s.ini_connects, alsa->id);
        remove_connects_involving(s.ini_connects, rtp->id);
        remove_peer_id(s.ini_peers, alsa->id);
        remove_peer_id(s.ini_peers, rtp->id);
        lowered = true;
        break;
      }
      if (lowered)
        break;
    }
    if (!lowered)
      break;
  }

  // Remaining peers must be factories (no connects may reference them)
  if (!s.ini_connects.empty())
    throw rtpmidid::exception(
        "INI leftover [connect] entries (unmatched graph) in {}", ini_filename);

  std::vector<settings_t::ini_peer_template_t> leftovers = std::move(s.ini_peers);
  s.ini_peers.clear();
  for (const auto &p : leftovers) {
    if (p.type == "listen_rtpmidi") {
      settings_t::rtpmidi_announce_t a;
      a.name = param_or_empty(p, "name");
      a.port = param_or_empty(p, "port");
      s.rtpmidi_announces.push_back(std::move(a));
    } else if (p.type == "listen_alsa_network") {
      settings_t::alsa_announce_t a;
      a.name = param_or_empty(p, "name");
      s.alsa_announces.push_back(std::move(a));
    } else {
      std::string edges;
      for (const auto &c : s.ini_connects)
        edges += c.from_id + "->" + c.to_id + " ";
      throw rtpmidid::exception(
          "INI unresolved peer id '{}' type '{}' (remaining connects [{}]) in {}",
          p.id, p.type, edges, ini_filename);
    }
  }
}

} // namespace rtpmididns

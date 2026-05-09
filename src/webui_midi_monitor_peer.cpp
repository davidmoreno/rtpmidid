/**
 * Web UI MIDI monitor peer implementation.
 */
#include "webui_midi_monitor_peer.hpp"
#include "midirouter.hpp"
#include <rtpmidid/logger.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace rtpmididns {

namespace {

std::mutex registry_mtx;
std::unordered_map<std::string, std::weak_ptr<webui_midi_monitor_peer_t>> registry;

std::string hex_prefix(const uint8_t *data, size_t len, size_t max_bytes = 24) {
  std::string out;
  const size_t n = len < max_bytes ? len : max_bytes;
  out.reserve(n * 3 + 24);
  for (size_t i = 0; i < n; ++i) {
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02x%s", data[i], (i + 1 < n) ? " " : "");
    out += buf;
  }
  if (len > max_bytes) {
    char tail[48];
    std::snprintf(tail, sizeof(tail), " … (+ %zu bytes)", len - max_bytes);
    out += tail;
  }
  return out;
}

} // namespace

webui_midi_monitor_peer_t::webui_midi_monitor_peer_t(std::string uuid_str,
                                                     midipeer_id_t target_peer_id,
                                                     std::string display_name)
    : uuid_(std::move(uuid_str)), target_peer_id_(target_peer_id),
      name_(std::move(display_name)) {}

const char *webui_midi_monitor_peer_t::get_type() const {
  return "webui_midi_monitor_peer_t";
}

router_peer_row_t webui_midi_monitor_peer_t::status() const {
  router_peer_row_t row{};
  row.name = name_;
  row.monitor_uuid = uuid_;
  row.monitor_target_peer_id =
      static_cast<uint64_t>(static_cast<uint32_t>(target_peer_id_));
  return row;
}

bool webui_midi_monitor_peer_t::try_set_ws_binary_sink(
    std::function<bool(const uint8_t *, size_t)> sink) {
  std::lock_guard<std::mutex> lock(sink_mtx_);
  if (ws_sink_)
    return false;
  ws_sink_ = std::move(sink);
  return true;
}

void webui_midi_monitor_peer_t::clear_ws_binary_sink() {
  std::lock_guard<std::mutex> lock(sink_mtx_);
  ws_sink_ = nullptr;
}

void webui_midi_monitor_peer_t::send_midi(midipeer_id_t from,
                                          const mididata_t &data) {
  const uint8_t *p = data.position;
  const size_t n = data.remaining();
  if (n == 0 || p == nullptr)
    return;

  std::function<bool(const uint8_t *, size_t)> sink;
  {
    std::lock_guard<std::mutex> lock(sink_mtx_);
    sink = ws_sink_;
  }

  if (!sink) {
    DEBUG(
        "webui_midi_monitor peer_id={} uuid={}… drop {} bytes from_peer={} "
        "(no WebSocket viewer)",
        peer_id, uuid_.substr(0, 8), n, from);
    return;
  }

  const bool sent_ok = sink(p, n);

  uint64_t tot_batches = 0;
  uint64_t tot_bytes = 0;
  bool emit_recv_stats = false;
  {
    std::lock_guard<std::mutex> lock(sink_mtx_);
    recv_batches_++;
    recv_bytes_ += n;
    tot_batches = recv_batches_;
    tot_bytes = recv_bytes_;

    const auto now = std::chrono::steady_clock::now();
    if (recv_stats_last_log_.time_since_epoch().count() == 0) {
      recv_stats_last_log_ = now;
      emit_recv_stats = true;
    } else if (now - recv_stats_last_log_ >= std::chrono::seconds(2)) {
      recv_stats_last_log_ = now;
      emit_recv_stats = true;
    }
  }
  if (!sent_ok) {
    WARNING_RATE_LIMIT(
        2,
        "webui_midi_monitor peer_id={} uuid={}… ws.send failed ({} bytes "
        "from_peer={})",
        peer_id, uuid_.substr(0, 8), n, from);
  }

  DEBUG(
      "webui_midi_monitor peer_id={} uuid={}… target_peer={} ← from_peer={} "
      "{} bytes [{}] ws_ok={}",
      peer_id, uuid_.substr(0, 8), target_peer_id_, from, n,
      hex_prefix(p, n), sent_ok);

  if (emit_recv_stats) {
    INFO(
        "webui_midi_monitor peer_id={} uuid={}… recv totals: {} bytes in {} "
        "router deliveries (target_peer={}). DEBUG per packet; restart monitor "
        "after router topology changes.",
        peer_id, uuid_.substr(0, 8), tot_bytes, tot_batches, target_peer_id_);
  }
}

std::shared_ptr<midipeer_t>
make_webui_midi_monitor_peer(std::string uuid_str, midipeer_id_t target_peer_id) {
  std::string nm = "WEB · MIDI monitor · " + uuid_str.substr(0, 8);
  return std::make_shared<webui_midi_monitor_peer_t>(
      std::move(uuid_str), target_peer_id, std::move(nm));
}

void monitor_registry_register(const std::string &uuid,
                               const std::shared_ptr<webui_midi_monitor_peer_t> &peer) {
  std::lock_guard<std::mutex> lock(registry_mtx);
  registry[uuid] = peer;
}

void monitor_registry_unregister(const std::string &uuid) {
  std::lock_guard<std::mutex> lock(registry_mtx);
  registry.erase(uuid);
}

std::shared_ptr<webui_midi_monitor_peer_t>
monitor_registry_lookup(const std::string &uuid) {
  std::lock_guard<std::mutex> lock(registry_mtx);
  auto it = registry.find(uuid);
  if (it == registry.end())
    return nullptr;
  return it->second.lock();
}

void monitor_session_stop(const std::shared_ptr<midirouter_t> &router,
                          const std::shared_ptr<webui_midi_monitor_peer_t> &peer) {
  if (!router || !peer || peer->peer_id == 0)
    return;
  const midipeer_id_t mid = peer->peer_id;
  const std::string uuid = peer->session_uuid();
  router->enqueue_remove_peer(mid);
  monitor_registry_unregister(uuid);
}

} // namespace rtpmididns

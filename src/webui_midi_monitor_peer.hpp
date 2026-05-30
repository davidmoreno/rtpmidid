/**
 * Web UI MIDI monitor sink: receives routed MIDI and forwards live to one WebSocket.
 */
#pragma once

#include "dm_json_status.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>

namespace rtpmididns {

class midirouter_t;

/** Router sink peer: MIDI streamed as binary WebSocket frames after `monitor.start`. */
class webui_midi_monitor_peer_t : public midipeer_t {
public:
  webui_midi_monitor_peer_t(std::string uuid_str, midipeer_id_t target_peer_id,
                             std::string display_name);

  const char *get_type() const override;
  std::optional<std::string> compute_stable_id() const override;
  router_peer_row_t status() const override;
  void send_midi(midipeer_id_t from, const mididata_t &data) override;

  const std::string &session_uuid() const { return uuid_; }
  midipeer_id_t monitor_target_peer_id() const { return target_peer_id_; }

  /**
   * Attach the single allowed viewer: send_midi forwards straight to this callback.
   * Returns false if another WebSocket already owns this session — caller must close.
   */
  bool try_set_ws_binary_sink(std::function<bool(const uint8_t *, size_t)> sink);
  void clear_ws_binary_sink();

private:
  std::string uuid_;
  midipeer_id_t target_peer_id_;
  std::string name_;

  mutable std::mutex sink_mtx_;
  std::function<bool(const uint8_t *, size_t)> ws_sink_;

  uint64_t recv_batches_{0};
  uint64_t recv_bytes_{0};
  std::chrono::steady_clock::time_point recv_stats_last_log_{};
};

std::shared_ptr<midipeer_t>
make_webui_midi_monitor_peer(std::string uuid_str, midipeer_id_t target_peer_id);

void monitor_registry_register(const std::string &uuid,
                               const std::shared_ptr<webui_midi_monitor_peer_t> &peer);
void monitor_registry_unregister(const std::string &uuid);
std::shared_ptr<webui_midi_monitor_peer_t>
monitor_registry_lookup(const std::string &uuid);

/** Remove monitor peer (router tears down all edges involving it), unregister session. */
void monitor_session_stop(const std::shared_ptr<midirouter_t> &router,
                          const std::shared_ptr<webui_midi_monitor_peer_t> &peer);

} // namespace rtpmididns

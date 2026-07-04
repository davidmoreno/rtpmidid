/**
 * Unified peer creation from device identity strings.
 */
#pragma once

#include "device_identity.hpp"
#include "midipeer.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rtpmidid {
class mdns_rtpmidi_t;
class rtpclient_t;
class rtppeer_t;
} // namespace rtpmidid

namespace rtpmididns {

class aseq_t;
class midirouter_t;

struct peer_factory_context_t {
  std::shared_ptr<aseq_t> aseq;
  std::shared_ptr<midirouter_t> router;
  std::shared_ptr<rtpmidid::mdns_rtpmidi_t> mdns;
};

enum class peer_attachment_kind_e { none, rtppeer, rtpclient };

struct peer_create_request_t {
  device_identity_t identity;
  peer_attachment_kind_e attachment = peer_attachment_kind_e::none;
  std::shared_ptr<rtpmidid::rtppeer_t> rtppeer;
  std::shared_ptr<rtpmidid::rtpclient_t> rtpclient;
  std::optional<peer_id_t> parent_peer_id;
  std::optional<peer_id_t> target_peer_id;
  std::optional<std::string> monitor_uuid;
};

std::optional<std::shared_ptr<midipeer_t>>
create_peer(peer_create_request_t req, const peer_factory_context_t &ctx,
            std::string *err = nullptr);

std::optional<std::shared_ptr<midipeer_t>>
create_peer_from_string(std::string_view identity, const peer_factory_context_t &ctx,
                        std::string *err = nullptr);

} // namespace rtpmididns

/**
 * Phase 1: peer_kind mapping and factory get_type() consistency.
 */
#include "../src/factory.hpp"
#include "../src/midipeer.hpp"
#include "../src/peer_kind.hpp"
#include "test_case.hpp"
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <cstring>
#include <memory>
#include <string>

using namespace rtpmididns;

namespace rtpmididns {
std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
} // namespace rtpmididns

namespace {

void assert_wire_roundtrip(peer_kind_e kind) {
  const char *wire = peer_kind_wire_type(kind);
  const auto parsed = peer_kind_from_wire_type(wire);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(*parsed == kind);
}

void assert_peer_type(const std::shared_ptr<midipeer_t> &peer,
                      peer_kind_e expected) {
  ASSERT_TRUE(peer != nullptr);
  ASSERT_EQUAL(std::strcmp(peer->get_type(), peer_kind_wire_type(expected)), 0);
  const auto kind = peer_kind_from_wire_type(peer->get_type());
  ASSERT_TRUE(kind.has_value());
  ASSERT_TRUE(*kind == expected);
}

} // namespace

void test_peer_kind_wire_roundtrip_all_kinds() {
  assert_wire_roundtrip(peer_kind_e::device_alsa_seq);
  assert_wire_roundtrip(peer_kind_e::device_rawmidi);
  assert_wire_roundtrip(peer_kind_e::device_rtpmidi_client);
  assert_wire_roundtrip(peer_kind_e::device_rtpmidi_session);
  assert_wire_roundtrip(peer_kind_e::export_alsa_network);
  assert_wire_roundtrip(peer_kind_e::export_rtpmidi_server);
  assert_wire_roundtrip(peer_kind_e::import_rtpmidi);
  assert_wire_roundtrip(peer_kind_e::import_alsa_rtp);
  assert_wire_roundtrip(peer_kind_e::webui_monitor);
}

void test_peer_kind_identity_prefixes() {
  ASSERT_EQUAL(std::strcmp(peer_kind_identity_prefix(peer_kind_e::device_alsa_seq),
                           "alsa_seq"),
               0);
  ASSERT_EQUAL(std::strcmp(peer_kind_identity_prefix(peer_kind_e::device_rawmidi),
                           "rawmidi"),
               0);
  ASSERT_EQUAL(std::strcmp(peer_kind_identity_prefix(peer_kind_e::export_rtpmidi_server),
                           "rtpmidi_server"),
               0);
  ASSERT_EQUAL(std::strcmp(peer_kind_identity_prefix(peer_kind_e::import_rtpmidi),
                           "rtpmidi_multi"),
               0);
  ASSERT_EQUAL(std::strcmp(peer_kind_identity_prefix(peer_kind_e::export_alsa_network),
                           "alsa_multi"),
               0);
  ASSERT_EQUAL(peer_kind_identity_prefix(peer_kind_e::webui_monitor)[0], '\0');
}

void test_peer_kind_classification_flags() {
  ASSERT_TRUE(peer_kind_is_device(peer_kind_e::device_alsa_seq));
  ASSERT_FALSE(peer_kind_is_device(peer_kind_e::export_rtpmidi_server));
  ASSERT_TRUE(peer_kind_is_export(peer_kind_e::export_alsa_network));
  ASSERT_TRUE(peer_kind_is_import(peer_kind_e::import_alsa_rtp));
  ASSERT_FALSE(peer_kind_is_import(peer_kind_e::device_rawmidi));
}

void test_factory_export_rtpmidi_server_type() {
  auto peer = make_peer_export_rtpmidi_server("factory-test", "50210");
  assert_peer_type(peer, peer_kind_e::export_rtpmidi_server);
}

void test_factory_rtpmidi_client_type() {
  auto peer = make_peer_device_rtpmidi_client("cli", "127.0.0.1", "5004");
  assert_peer_type(peer, peer_kind_e::device_rtpmidi_client);
}

void test_peer_kind_rpc_create_keys() {
  ASSERT_TRUE(peer_kind_rpc_create_key(peer_kind_e::device_rawmidi).has_value());
  ASSERT_EQUAL(std::strcmp(*peer_kind_rpc_create_key(peer_kind_e::device_rawmidi),
                           "local_rawmidi_t"),
               0);
  ASSERT_FALSE(peer_kind_rpc_create_key(peer_kind_e::import_rtpmidi).has_value());
}

void test_factory_export_rtpmidi_server_stable_id() {
  auto peer = make_peer_export_rtpmidi_server("factory-test", "50211");
  const auto sid = peer->compute_stable_id();
  ASSERT_TRUE(sid.has_value());
  ASSERT_EQUAL(*sid, std::string("rtpmidi_server:factory-test"));
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_peer_kind_wire_roundtrip_all_kinds),
      TEST(test_peer_kind_identity_prefixes),
      TEST(test_peer_kind_classification_flags),
      TEST(test_factory_export_rtpmidi_server_type),
      TEST(test_factory_export_rtpmidi_server_stable_id),
      TEST(test_factory_rtpmidi_client_type),
      TEST(test_peer_kind_rpc_create_keys),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

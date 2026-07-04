/**
 * peer_kind mapping and peer_factory get_type() consistency.
 */
#include "../src/device_identity_from_peer.hpp"
#include "../src/peer_factory.hpp"
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

peer_factory_context_t empty_factory_ctx() { return peer_factory_context_t{}; }

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

void test_factory_export_rtpmidi_server_type() {
  std::string err;
  auto peer = create_peer_from_string("rtpmidi_server:name=factory-test,port=50210",
                                      empty_factory_ctx(), &err);
  ASSERT_TRUE(peer.has_value());
  assert_peer_type(*peer, peer_kind_e::export_rtpmidi_server);
}

void test_factory_rtpmidi_client_type() {
  std::string err;
  auto peer = create_peer_from_string(
      "rtpmidi_client:hostname=127.0.0.1,port=5004,service=cli",
      empty_factory_ctx(), &err);
  ASSERT_TRUE(peer.has_value());
  assert_peer_type(*peer, peer_kind_e::device_rtpmidi_client);
}

void test_factory_export_rtpmidi_server_device_identity() {
  std::string err;
  auto peer = create_peer_from_string("rtpmidi_server:name=factory-test,port=50211",
                                      empty_factory_ctx(), &err);
  ASSERT_TRUE(peer.has_value());
  router_peer_row_t row = (*peer)->status();
  row.type = (*peer)->get_type();
  const auto id = compute_device_identity(row);
  ASSERT_TRUE(id.has_value());
  ASSERT_TRUE(id->serialize().find("rtpmidi_server:name=factory-test") == 0);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_peer_kind_wire_roundtrip_all_kinds),
      TEST(test_peer_kind_identity_prefixes),
      TEST(test_factory_export_rtpmidi_server_type),
      TEST(test_factory_export_rtpmidi_server_device_identity),
      TEST(test_factory_rtpmidi_client_type),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

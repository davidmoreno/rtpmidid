/**
 * Round-trip peer creation from device identity strings.
 */
#include "../src/device_identity_from_peer.hpp"
#include "../src/peer_factory.hpp"
#include "../src/peer_kind.hpp"
#include "test_case.hpp"
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <rtpmidid/rtppeer.hpp>
#include <cstring>
#include <memory>

using namespace rtpmididns;

namespace rtpmididns {
std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
} // namespace rtpmididns

namespace {

peer_factory_context_t empty_ctx() { return peer_factory_context_t{}; }

void assert_creates(const char *identity, peer_kind_e expected) {
  std::string err;
  auto peer = create_peer_from_string(identity, empty_ctx(), &err);
  ASSERT_TRUE(peer.has_value());
  ASSERT_EQUAL(std::strcmp((*peer)->get_type(), peer_kind_wire_type(expected)), 0);
}

} // namespace

void test_create_rawmidi() {
  assert_creates("rawmidi:device=/dev/snd/midiC0D0,name=Export",
                 peer_kind_e::device_rawmidi);
}

void test_create_rtpmidi_server() {
  assert_creates("rtpmidi_server:name=MyServer,port=5104",
                 peer_kind_e::export_rtpmidi_server);
}

void test_create_rtpmidi_client() {
  assert_creates(
      "rtpmidi_client:hostname=192.168.1.1,port=5004,service=Remote",
      peer_kind_e::device_rtpmidi_client);
}

void test_reject_rtpmidi_session_without_attachment() {
  std::string err;
  auto peer = create_peer_from_string(
      "rtpmidi_session:hostname=host.local,service=Peak", empty_ctx(), &err);
  ASSERT_FALSE(peer.has_value());
  ASSERT_TRUE(err.find("requires active RTP connection") != std::string::npos);
}

void test_reject_invalid_identity() {
  std::string err;
  auto peer = create_peer_from_string("peer:123", empty_ctx(), &err);
  ASSERT_FALSE(peer.has_value());
}

void test_create_rtpmidi_session_with_attachment() {
  auto rtp = std::make_shared<rtpmidid::rtppeer_t>("local");
  rtp->remote_name = "Remote Synth";

  peer_create_request_t req;
  req.identity = *device_identity_t::parse("rtpmidi_session:name=Remote Synth");
  req.attachment = peer_attachment_kind_e::rtppeer;
  req.rtppeer = rtp;

  std::string err;
  auto peer = create_peer(req, empty_ctx(), &err);
  ASSERT_TRUE(peer.has_value());
  ASSERT_EQUAL(std::strcmp((*peer)->get_type(),
                           peer_kind_wire_type(peer_kind_e::device_rtpmidi_session)),
               0);
}

void test_identity_from_rtppeer_roundtrip() {
  auto rtp = std::make_shared<rtpmidid::rtppeer_t>("local");
  rtp->remote_name = "Peak Out";
  const auto id = identity_from_rtppeer(*rtp);
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->type_prefix, "rtpmidi_session");
  ASSERT_EQUAL(id->find("name").value_or(""), "Peak Out");
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_create_rawmidi),
      TEST(test_create_rtpmidi_server),
      TEST(test_create_rtpmidi_client),
      TEST(test_reject_rtpmidi_session_without_attachment),
      TEST(test_reject_invalid_identity),
      TEST(test_create_rtpmidi_session_with_attachment),
      TEST(test_identity_from_rtppeer_roundtrip),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

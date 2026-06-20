/**
 * Phase 2: device_identity_t grammar tests.
 */
#include "../src/device_identity.hpp"
#include "test_case.hpp"
#include <string>

using namespace rtpmididns;

namespace {

device_identity_t make_id(std::string type,
                          std::vector<device_identity_field_t> fields) {
  return device_identity_t{std::move(type), std::move(fields)};
}

void assert_roundtrip(const std::string &canonical) {
  const auto parsed = device_identity_t::parse(canonical);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQUAL(parsed->serialize(), canonical);
}

void assert_parse_fails(std::string_view text) {
  ASSERT_FALSE(device_identity_t::parse(text).has_value());
}

} // namespace

void test_device_identity_roundtrip_simple() {
  assert_roundtrip("alsa_seq:client=Peak,port=In");
  assert_roundtrip("rawmidi:device=/dev/snd/midiC4D0,name=MIDI Export");
  assert_roundtrip(
      "rtpmidi_client:hostname=host.local,port=5004,service=Peak Out");
  assert_roundtrip("rtpmidi_server:name=Peak InOut,port=5004");
}

void test_device_identity_roundtrip_bracketed() {
  assert_roundtrip("rtpmidi_server:name=Peak,[port=5004]");
  assert_roundtrip("alsa_seq:client=Peak,[port=In]");
  assert_roundtrip(
      "rtpmidi_client:hostname=host.local,[port=5004],service=Peak Out");
}

void test_device_identity_field_order_canonicalization() {
  const auto parsed = device_identity_t::parse(
      "rtpmidi_server:port=5004,name=Peak");
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQUAL(parsed->serialize(), "rtpmidi_server:name=Peak,port=5004");

  const auto built =
      make_id("alsa_seq", {{"port", "In", false}, {"client", "Peak", false}});
  ASSERT_EQUAL(built.serialize(), "alsa_seq:client=Peak,port=In");
}

void test_device_identity_bracketed_order_canonicalization() {
  const auto parsed = device_identity_t::parse(
      "rtpmidi_server:[port=5004],name=Peak");
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQUAL(parsed->serialize(), "rtpmidi_server:name=Peak,[port=5004]");
}

void test_device_identity_escape_special_chars() {
  const auto parsed = device_identity_t::parse(
      R"(rawmidi:device=/dev/snd/midiC4D0,name=MIDI\, Export\: v1)");
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQUAL(parsed->fields[1].value, "MIDI, Export: v1");
  ASSERT_EQUAL(parsed->serialize(),
               R"(rawmidi:device=/dev/snd/midiC4D0,name=MIDI\, Export\: v1)");

  const auto brackets = device_identity_t::parse(
      R"(alsa_seq:client=Peak,[port=\[In\]])");
  ASSERT_TRUE(brackets.has_value());
  ASSERT_TRUE(brackets->fields[1].bracketed);
  ASSERT_EQUAL(brackets->fields[1].value, "[In]");
  ASSERT_EQUAL(brackets->serialize(), R"(alsa_seq:client=Peak,[port=\[In\]])");
}

void test_device_identity_escape_roundtrip_unit() {
  const std::string raw = R"(a,b=c:d=e[f]g\)";
  const auto escaped = device_identity_t::escape(raw);
  ASSERT_EQUAL(device_identity_t::unescape(escaped), raw);
}

void test_device_identity_bracketed_preserved() {
  const auto id = make_id("rtpmidi_server",
                          {{"name", "Peak", false},
                           {"port", "5004", true}});
  ASSERT_EQUAL(id.serialize(), "rtpmidi_server:name=Peak,[port=5004]");

  const auto reparsed = device_identity_t::parse(id.serialize());
  ASSERT_TRUE(reparsed.has_value());
  ASSERT_TRUE(*reparsed == id);
}

void test_device_identity_equality() {
  const auto a = device_identity_t::parse("alsa_seq:client=Peak,port=In");
  const auto b = device_identity_t::parse("alsa_seq:port=In,client=Peak");
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  ASSERT_TRUE(*a == *b);
}

void test_device_identity_parse_errors() {
  assert_parse_fails("");
  assert_parse_fails("no_colon");
  assert_parse_fails(":key=value");
  assert_parse_fails("type:");
  assert_parse_fails("type:key");
  assert_parse_fails("type:key=");
  assert_parse_fails("type:[key=value");
  assert_parse_fails("type:key=value,");
  assert_parse_fails("type:duplicate=1,duplicate=2");
}

void test_canonical_key_strips_port_from_rtpmidi_client() {
  const auto a = device_identity_t::parse(
      "rtpmidi_client:hostname=rasppi32.local,port=36627,service=Hydra");
  const auto b = device_identity_t::parse(
      "rtpmidi_client:hostname=rasppi32.local,port=37740,service=Hydra");
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  ASSERT_EQUAL(a->canonical_key(), b->canonical_key());
  ASSERT_EQUAL(a->canonical_key(),
               "rtpmidi_client:hostname=rasppi32.local,service=Hydra");
}

void test_canonical_key_strips_port_from_rtpmidi_server() {
  const auto a = device_identity_t::parse(
      "rtpmidi_server:name=devel-Hydra,port=42372");
  const auto b = device_identity_t::parse(
      "rtpmidi_server:name=devel-Hydra,port=55545");
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  ASSERT_EQUAL(a->canonical_key(), b->canonical_key());
  ASSERT_EQUAL(a->canonical_key(), "rtpmidi_server:name=devel-Hydra");
}

void test_canonical_key_preserves_client_port_when_no_name() {
  // alsa_seq without name: client+port ARE the identity
  const auto id = device_identity_t::parse("alsa_seq:client=Peak,port=In");
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->canonical_key(), "alsa_seq:client=Peak,port=In");
}

void test_canonical_key_strips_client_port_when_name_present() {
  const auto id = device_identity_t::parse(
      "alsa_seq:client=28,name=Hydra,port=0");
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->canonical_key(), "alsa_seq:name=Hydra");
}

void test_canonical_key_strips_hostname_port_from_alsa_listener() {
  const auto id = device_identity_t::parse(
      "alsa_listener:hostname=rasppi32.local,port=36627,"
      "local_udp_port=12345,service=Hydra");
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->canonical_key(), "alsa_listener:service=Hydra");
}

void test_canonical_key_preserves_all_for_rawmidi() {
  const auto id = device_identity_t::parse(
      "rawmidi:device=/dev/snd/midiC4D0,name=MIDI Export");
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->canonical_key(),
               "rawmidi:device=/dev/snd/midiC4D0,name=MIDI Export");
}

void test_canonical_key_strips_port_from_rtpmidi_multi() {
  const auto id = device_identity_t::parse(
      "rtpmidi_multi:name=devel,port=5004");
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->canonical_key(), "rtpmidi_multi:name=devel");
}

void test_canonical_key_preserves_all_for_alsa_multi() {
  const auto id = device_identity_t::parse("alsa_multi:name=Network Export");
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->canonical_key(), "alsa_multi:name=Network Export");
}

void test_canonical_key_rtpmidi_session_preserves_hostname_service() {
  // rtpmidi_session has no ephemeral fields defined.
  // Note: colons in the hostname are escape-serialized.
  const auto id = device_identity_t::parse(
      "rtpmidi_session:hostname=::ffff:192.168.1.78,service=Hydra");
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->canonical_key(),
               "rtpmidi_session:hostname=\\:\\:ffff\\:192.168.1.78,"
               "service=Hydra");
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_device_identity_roundtrip_simple),
      TEST(test_device_identity_roundtrip_bracketed),
      TEST(test_device_identity_field_order_canonicalization),
      TEST(test_device_identity_bracketed_order_canonicalization),
      TEST(test_device_identity_escape_special_chars),
      TEST(test_device_identity_escape_roundtrip_unit),
      TEST(test_device_identity_bracketed_preserved),
      TEST(test_device_identity_equality),
      TEST(test_device_identity_parse_errors),
      TEST(test_canonical_key_strips_port_from_rtpmidi_client),
      TEST(test_canonical_key_strips_port_from_rtpmidi_server),
      TEST(test_canonical_key_preserves_client_port_when_no_name),
      TEST(test_canonical_key_strips_client_port_when_name_present),
      TEST(test_canonical_key_strips_hostname_port_from_alsa_listener),
      TEST(test_canonical_key_preserves_all_for_rawmidi),
      TEST(test_canonical_key_strips_port_from_rtpmidi_multi),
      TEST(test_canonical_key_preserves_all_for_alsa_multi),
      TEST(test_canonical_key_rtpmidi_session_preserves_hostname_service),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

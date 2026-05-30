/**
 * Phase 3: device_query_t matching tests.
 */
#include "../src/device_identity.hpp"
#include "../src/device_query.hpp"
#include "test_case.hpp"
#include <string>
#include <vector>

using namespace rtpmididns;

namespace {

device_identity_t id(std::string type,
                     std::initializer_list<std::pair<const char *, const char *>> fields) {
  device_identity_t out;
  out.type_prefix = std::move(type);
  for (const auto &[k, v] : fields) {
    out.fields.push_back({k, v, false});
  }
  return out;
}

device_query_t query(std::string type,
                     std::initializer_list<std::tuple<const char *, const char *, bool>>
                         fields) {
  device_query_t out;
  out.type_prefix = std::move(type);
  for (const auto &[k, v, bracketed] : fields) {
    out.fields.push_back({k, v, bracketed});
  }
  return out;
}

std::vector<size_t> match_indices(const device_query_t &q,
                                  const std::vector<device_identity_t> &devices) {
  return find_all_matching(q, devices);
}

} // namespace

void test_device_query_single_match() {
  const std::vector devices = {
      id("rtpmidi_server", {{"name", "Peak"}, {"port", "5004"}}),
      id("rtpmidi_server", {{"name", "Hydrasynth"}, {"port", "5004"}}),
  };

  const auto q = query("rtpmidi_server", {{"name", "Peak", false}});
  const auto hits = match_indices(q, devices);
  ASSERT_EQUAL(hits.size(), size_t{1});
  ASSERT_EQUAL(hits[0], size_t{0});
  ASSERT_TRUE(q.matches(devices[0]));
  ASSERT_FALSE(q.matches(devices[1]));
}

void test_device_query_multi_match_fanout() {
  const std::vector devices = {
      id("alsa_seq", {{"client", "Peak"}, {"port", "In"}}),
      id("alsa_seq", {{"client", "Peak"}, {"port", "Out"}}),
      id("alsa_seq", {{"client", "Hydrasynth"}, {"port", "In"}}),
  };

  const auto q = query("alsa_seq", {{"client", "Peak", false}});
  const auto hits = match_indices(q, devices);
  ASSERT_EQUAL(hits.size(), size_t{2});
  ASSERT_EQUAL(hits[0], size_t{0});
  ASSERT_EQUAL(hits[1], size_t{1});
}

void test_device_query_optional_field_omitted_matches_all() {
  const std::vector devices = {
      id("rtpmidi_client",
         {{"hostname", "host.local"}, {"service", "Peak Out"}, {"port", "5004"}}),
      id("rtpmidi_client",
         {{"hostname", "host.local"}, {"service", "Peak In"}, {"port", "5005"}}),
      id("rtpmidi_client",
         {{"hostname", "other.local"}, {"service", "Peak Out"}, {"port", "5004"}}),
  };

  const auto q = query("rtpmidi_client", {{"hostname", "host.local", false}});
  const auto hits = match_indices(q, devices);
  ASSERT_EQUAL(hits.size(), size_t{2});
  ASSERT_TRUE(q.matches(devices[0]));
  ASSERT_TRUE(q.matches(devices[1]));
  ASSERT_FALSE(q.matches(devices[2]));
}

void test_device_query_bracketed_field_does_not_narrow() {
  const std::vector devices = {
      id("rtpmidi_server", {{"name", "Peak"}, {"port", "5004"}}),
      id("rtpmidi_server", {{"name", "Peak"}, {"port", "4001"}}),
  };

  const auto narrow = query("rtpmidi_server",
                            {{"name", "Peak", false}, {"port", "5004", false}});
  ASSERT_TRUE(narrow.matches(devices[0]));
  ASSERT_FALSE(narrow.matches(devices[1]));

  const auto stored = query("rtpmidi_server",
                            {{"name", "Peak", false}, {"port", "5004", true}});
  ASSERT_TRUE(stored.matches(devices[0]));
  ASSERT_TRUE(stored.matches(devices[1]));

  const auto parsed = device_query_t::parse("rtpmidi_server:name=Peak,[port=5004]");
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->matches(devices[0]));
  ASSERT_TRUE(parsed->matches(devices[1]));
}

void test_device_query_mismatch() {
  const auto device =
      id("rawmidi", {{"device", "/dev/snd/midiC4D0"}, {"name", "MIDI Export"}});

  const auto wrong_type = query("alsa_seq", {{"client", "Peak", false}});
  ASSERT_FALSE(wrong_type.matches(device));

  const auto wrong_value =
      query("rawmidi", {{"device", "/dev/snd/midiC4D1", false}});
  ASSERT_FALSE(wrong_value.matches(device));

  const auto missing_key =
      query("rawmidi", {{"name", "MIDI Export", false}, {"device", "/dev/snd/midiC4D0", false}});
  ASSERT_TRUE(missing_key.matches(device));

  const auto extra_constraint =
      query("rawmidi", {{"device", "/dev/snd/midiC4D0", false},
                        {"name", "Other", false}});
  ASSERT_FALSE(extra_constraint.matches(device));
}

void test_device_query_bracketed_only_matches_by_type() {
  const std::vector devices = {
      id("rtpmidi_server", {{"name", "Peak"}, {"port", "5004"}}),
      id("rtpmidi_server", {{"name", "Other"}, {"port", "5005"}}),
      id("alsa_seq", {{"client", "Peak"}, {"port", "In"}}),
  };

  const auto q = query("rtpmidi_server", {{"port", "5004", true}});
  const auto hits = match_indices(q, devices);
  ASSERT_EQUAL(hits.size(), size_t{2});
  ASSERT_EQUAL(hits[0], size_t{0});
  ASSERT_EQUAL(hits[1], size_t{1});
}

void test_device_query_serialize_roundtrip() {
  const auto q = query("rtpmidi_server",
                       {{"name", "Peak", false}, {"port", "5004", true}});
  const auto reparsed = device_query_t::parse(q.serialize());
  ASSERT_TRUE(reparsed.has_value());
  ASSERT_EQUAL(reparsed->serialize(), q.serialize());
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_device_query_single_match),
      TEST(test_device_query_multi_match_fanout),
      TEST(test_device_query_optional_field_omitted_matches_all),
      TEST(test_device_query_bracketed_field_does_not_narrow),
      TEST(test_device_query_mismatch),
      TEST(test_device_query_bracketed_only_matches_by_type),
      TEST(test_device_query_serialize_roundtrip),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

/**
 * Phase 7: pure-ALSA direct connection planning tests.
 */
#include "../src/connection_alsa_direct.hpp"
#include "test_case.hpp"

#include <string>
#include <vector>

using namespace rtpmididns;

namespace {

alsa_seq_port_row_t port_row(int client, int port, std::string client_name,
                             std::string port_name) {
  alsa_seq_port_row_t row{};
  row.client = client;
  row.port = port;
  row.client_name = std::move(client_name);
  row.port_name = std::move(port_name);
  return row;
}

bool has_link(const std::vector<alsa_aconnect_action_t> &actions, int fc,
              int fp, int tc, int tp) {
  const aseq_t::port_t from{static_cast<uint8_t>(fc), static_cast<uint8_t>(fp)};
  const aseq_t::port_t to{static_cast<uint8_t>(tc), static_cast<uint8_t>(tp)};
  for (const auto &a : actions) {
    if (a.from == from && a.to == to)
      return true;
  }
  return false;
}

} // namespace

void test_is_direct_alsa_side() {
  ASSERT_TRUE(is_direct_alsa_side("alsa_seq:client=Peak,port=In"));
  ASSERT_TRUE(is_direct_alsa_side("alsa_seq:client=Peak,[port=In]"));
  ASSERT_FALSE(is_direct_alsa_side("rawmidi:device=/dev/snd/midiC0D0"));
  ASSERT_FALSE(is_direct_alsa_side("alsa:Peak:In"));
}

void test_match_alsa_ports_identity() {
  const std::vector<alsa_seq_port_row_t> ports = {
      port_row(1, 0, "Alpha", "OUT"),
      port_row(2, 0, "Beta", "IN"),
  };

  const auto by_identity =
      match_alsa_ports_for_side("alsa_seq:client=Alpha,port=OUT", ports);
  ASSERT_EQUAL(by_identity.size(), 1u);
  ASSERT_EQUAL(by_identity[0], 0u);
}

void test_match_alsa_ports_query_fanout() {
  const std::vector<alsa_seq_port_row_t> ports = {
      port_row(1, 0, "Peak", "Out A"),
      port_row(1, 1, "Peak", "Out B"),
      port_row(2, 0, "Synth", "In"),
  };

  const auto matches =
      match_alsa_ports_for_side("alsa_seq:client=Peak,[port=Out]", ports);
  ASSERT_EQUAL(matches.size(), 2u);
}

void test_plan_alsa_aconnect_direction() {
  const std::vector<alsa_seq_port_row_t> ports = {
      port_row(1, 0, "Alpha", "OUT"),
      port_row(2, 0, "Beta", "IN"),
  };

  stored_connection_t row;
  row.side_a = "alsa_seq:client=Alpha,port=OUT";
  row.side_b = "alsa_seq:client=Beta,port=IN";
  row.enabled = true;

  row.direction = connection_direction_e::a2b;
  auto a2b = plan_alsa_aconnect_actions({row}, ports);
  ASSERT_EQUAL(a2b.size(), 1u);
  ASSERT_TRUE(has_link(a2b, 1, 0, 2, 0));
  ASSERT_FALSE(has_link(a2b, 2, 0, 1, 0));

  row.direction = connection_direction_e::b2a;
  auto b2a = plan_alsa_aconnect_actions({row}, ports);
  ASSERT_EQUAL(b2a.size(), 1u);
  ASSERT_TRUE(has_link(b2a, 2, 0, 1, 0));

  row.direction = connection_direction_e::both;
  auto both = plan_alsa_aconnect_actions({row}, ports);
  ASSERT_EQUAL(both.size(), 2u);
  ASSERT_TRUE(has_link(both, 1, 0, 2, 0));
  ASSERT_TRUE(has_link(both, 2, 0, 1, 0));
}

void test_plan_alsa_aconnect_query_cartesian() {
  const std::vector<alsa_seq_port_row_t> ports = {
      port_row(1, 0, "Peak", "Out A"),
      port_row(1, 1, "Peak", "Out B"),
      port_row(2, 0, "Synth", "In"),
  };

  stored_connection_t row;
  row.side_a = "alsa_seq:client=Peak,[port=Out]";
  row.side_b = "alsa_seq:client=Synth,port=In";
  row.direction = connection_direction_e::a2b;
  row.enabled = true;

  const auto actions = plan_alsa_aconnect_actions({row}, ports);
  ASSERT_EQUAL(actions.size(), 2u);
  ASSERT_TRUE(has_link(actions, 1, 0, 2, 0));
  ASSERT_TRUE(has_link(actions, 1, 1, 2, 0));
}

void test_plan_skips_non_alsa_and_disabled() {
  const std::vector<alsa_seq_port_row_t> ports = {
      port_row(1, 0, "Alpha", "OUT"),
      port_row(2, 0, "Beta", "IN"),
  };

  stored_connection_t disabled;
  disabled.side_a = "alsa_seq:client=Alpha,port=OUT";
  disabled.side_b = "alsa_seq:client=Beta,port=IN";
  disabled.direction = connection_direction_e::both;
  disabled.enabled = false;

  stored_connection_t mixed;
  mixed.side_a = "alsa_seq:client=Alpha,port=OUT";
  mixed.side_b = "rawmidi:device=/dev/snd/midiC0D0";
  mixed.direction = connection_direction_e::both;
  mixed.enabled = true;

  const auto actions =
      plan_alsa_aconnect_actions({disabled, mixed}, ports);
  ASSERT_EQUAL(actions.size(), 0u);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_is_direct_alsa_side),
      TEST(test_match_alsa_ports_identity),
      TEST(test_match_alsa_ports_query_fanout),
      TEST(test_plan_alsa_aconnect_direction),
      TEST(test_plan_alsa_aconnect_query_cartesian),
      TEST(test_plan_skips_non_alsa_and_disabled),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

/**
 * Phase 7: ALSA monitor tap smoke / router-edge tests.
 */
#include "../src/alsa_monitor_tap.hpp"
#include "../src/midipeer.hpp"
#include "../src/midirouter.hpp"
#include "test_case.hpp"

#include <memory>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <string>

using namespace rtpmididns;

namespace rtpmididns {
std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
} // namespace rtpmididns

namespace {

class tap_test_peer_t : public midipeer_t {
public:
  explicit tap_test_peer_t(std::string name) : name_(std::move(name)) {}

  void send_midi(midipeer_id_t, const mididata_t &) override {}
  const char *get_type() const override { return "tap_test_peer_t"; }
  router_peer_row_t status() const override {
    router_peer_row_t row;
    row.name = name_;
    return row;
  }

private:
  std::string name_;
};

bool router_has_edge(const midirouter_t &router, peer_id_t from, peer_id_t to) {
  const auto targets = router.send_targets_for(from);
  for (const auto t : targets) {
    if (t == to)
      return true;
  }
  return false;
}

} // namespace

void test_teardown_unknown_session_is_noop() {
  auto router = std::make_shared<midirouter_t>();
  teardown_alsa_monitor_taps(router, "missing-uuid");
}

void test_setup_null_aseq_is_noop() {
  auto router = std::make_shared<midirouter_t>();
  const peer_id_t monitor = router->add_peer(std::make_shared<tap_test_peer_t>("mon"));
  setup_alsa_monitor_taps(nullptr, router, 1, 0, monitor, monitor, "sess",
                          [](uint8_t, uint8_t) { return peer_id_t{0}; });
  ASSERT_FALSE(router_has_edge(*router, monitor, monitor));
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_teardown_unknown_session_is_noop),
      TEST(test_setup_null_aseq_is_noop),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

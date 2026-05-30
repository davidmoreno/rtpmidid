/**
 * Spawn helpers: ensure_peer_for_identity matches online devices.
 */
#include "../src/peer_spawn.hpp"
#include "test_case.hpp"
#include "test_fake_peer.hpp"
#include <memory>
#include <rtpmidid/mdns_rtpmidi.hpp>

using namespace rtpmididns;

namespace rtpmididns {
std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
} // namespace rtpmididns

void test_ensure_peer_finds_online_identity() {
  auto router = std::make_shared<midirouter_t>();
  router->start_router_thread();

  auto fake = std::make_shared<fake_alsa_seq_peer_t>("test", "Peak", "In");
  const auto existing = router->add_peer(fake);

  peer_factory_context_t ctx;
  ctx.router = router;

  const auto found =
      ensure_peer_for_identity(ctx, router, "alsa_seq:client=Peak,port=In");
  ASSERT_EQUAL(found, existing);

  router->stop_router_thread();
}

void test_ensure_peer_rejects_stored_query() {
  auto router = std::make_shared<midirouter_t>();
  router->start_router_thread();

  peer_factory_context_t ctx;
  ctx.router = router;

  bool threw = false;
  try {
    ensure_peer_for_identity(ctx, router, "alsa_seq:client=Peak,[port=In]");
  } catch (const std::exception &) {
    threw = true;
  }
  ASSERT_TRUE(threw);

  router->stop_router_thread();
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_ensure_peer_finds_online_identity),
      TEST(test_ensure_peer_rejects_stored_query),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}

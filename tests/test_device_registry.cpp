/**
 * Phase 4: device registry and identity-from-peer tests.
 */
#include "../src/device_identity_from_peer.hpp"
#include "../src/device_registry.hpp"
#include "../src/factory.hpp"
#include "../src/midipeer.hpp"
#include "../src/midirouter.hpp"
#include "../src/peer_kind.hpp"
#include "test_case.hpp"
#include <memory>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <string>

using namespace rtpmididns;

namespace rtpmididns {
std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
} // namespace rtpmididns

namespace {

class test_midiio_t : public midipeer_t {
public:
  explicit test_midiio_t(std::string peer_name) : name_(std::move(peer_name)) {}

  void send_midi(midipeer_id_t /*from*/, const mididata_t &) override {}
  const char *get_type() const override { return peer_kind_wire_type(peer_kind_e::device_alsa_seq); }
  router_peer_row_t status() const override {
    router_peer_row_t row;
    row.type = get_type();
    row.name = name_;
    alsa_subscribe_from_t sub;
    sub.client_name = "Peak";
    sub.port_name = "In";
    row.alsa_subscribe_from = sub;
    return row;
  }

private:
  std::string name_;
};

} // namespace

void test_compute_device_identity_alsa_seq() {
  router_peer_row_t row;
  row.type = peer_kind_wire_type(peer_kind_e::device_alsa_seq);
  alsa_subscribe_from_t sub;
  sub.client_name = "Peak";
  sub.port_name = "In";
  row.alsa_subscribe_from = sub;

  const auto id = compute_device_identity(row);
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->serialize(), "alsa_seq:client=Peak,port=In");
}

void test_compute_device_identity_rawmidi() {
  router_peer_row_t row;
  row.type = peer_kind_wire_type(peer_kind_e::device_rawmidi);
  row.device = "/dev/snd/midiC4D0";
  row.name = "MIDI Export";

  const auto id = compute_device_identity(row);
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->serialize(),
               "rawmidi:device=/dev/snd/midiC4D0,name=MIDI Export");
}

void test_compute_device_identity_rtpmidi_client() {
  router_peer_row_t row;
  row.type = peer_kind_wire_type(peer_kind_e::device_rtpmidi_client);
  row.connect_hostname = "host.local";
  row.connect_port = "5004";
  rtp_peer_status_t peer;
  peer.remote.hostname = "ignored.local";
  peer.remote.name = "Peak Out";
  row.peer = peer;

  const auto id = compute_device_identity(row);
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->serialize(),
               "rtpmidi_client:hostname=host.local,port=5004,service=Peak Out");
}

void test_compute_device_identity_rtpmidi_server_factory() {
  auto peer = make_peer_export_rtpmidi_server("Peak InOut", "50220");
  const auto row = peer->status();
  router_peer_row_t full = row;
  full.type = peer->get_type();
  const auto id = compute_device_identity(full);
  ASSERT_TRUE(id.has_value());
  ASSERT_TRUE(id->serialize().find("rtpmidi_server:name=Peak InOut") == 0);
  ASSERT_TRUE(id->serialize().find("port=") != std::string::npos);
}

void test_compute_device_identity_rtpmidi_multi() {
  router_peer_row_t row;
  row.type = peer_kind_wire_type(peer_kind_e::import_rtpmidi);
  row.name = "Network Import";
  listening_ports_t listening;
  listening.name = "Network Import";
  listening.midi_port = 5004;
  listening.control_port = 5005;
  row.listening = listening;

  const auto id = compute_device_identity(row);
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->serialize(), "rtpmidi_multi:name=Network Import,port=5004");
}

void test_compute_device_identity_alsa_multi() {
  router_peer_row_t row;
  row.type = peer_kind_wire_type(peer_kind_e::export_alsa_network);
  row.name = "Network Export";

  const auto id = compute_device_identity(row);
  ASSERT_TRUE(id.has_value());
  ASSERT_EQUAL(id->serialize(), "alsa_multi:name=Network Export");
}

void test_registry_peer_appears_online() {
  auto router = std::make_shared<midirouter_t>();
  device_registry_t registry(router);
  registry.attach();

  auto peer = std::make_shared<test_midiio_t>("peak-in");
  const auto peer_id = router->add_peer(peer);

  const auto rec = registry.find_by_identity_key("alsa_seq:client=Peak,port=In");
  ASSERT_TRUE(rec.has_value());
  ASSERT_TRUE(rec->online());
  ASSERT_EQUAL(*rec->online_peer_id, peer_id);
  ASSERT_TRUE(rec->source == device_source_e::discovered);
}

void test_registry_peer_removed_offline_still_listed() {
  auto router = std::make_shared<midirouter_t>();
  device_registry_t registry(router);
  registry.attach();

  auto peer = std::make_shared<test_midiio_t>("peak-in");
  router->add_peer(peer);
  const std::string key = "alsa_seq:client=Peak,port=In";

  router->remove_peer(peer->peer_id);

  const auto rec = registry.find_by_identity_key(key);
  ASSERT_TRUE(rec.has_value());
  ASSERT_FALSE(rec->online());
}

void test_registry_merge_ini_manual_discovered() {
  auto router = std::make_shared<midirouter_t>();
  device_registry_t registry(router);
  registry.attach();

  const std::string key = "alsa_seq:client=Peak,port=In";
  registry.seed_ini({*device_identity_t::parse(key)});

  auto peer = std::make_shared<test_midiio_t>("peak-in");
  router->add_peer(peer);

  auto rec = registry.find_by_identity_key(key);
  ASSERT_TRUE(rec.has_value());
  ASSERT_TRUE(rec->online());
  ASSERT_TRUE(rec->source == device_source_e::ini);
  ASSERT_EQUAL(registry.list_devices().size(), size_t{1});

  registry.add_manual(*device_identity_t::parse(key), "Peak manual");
  rec = registry.find_by_identity_key(key);
  ASSERT_TRUE(rec.has_value());
  ASSERT_TRUE(rec->source == device_source_e::manual);
  ASSERT_EQUAL(registry.list_devices().size(), size_t{1});
}

void test_registry_ini_seed_without_peer() {
  auto router = std::make_shared<midirouter_t>();
  device_registry_t registry(router);

  const auto ini_id = *device_identity_t::parse("alsa_multi:name=Network Export");
  registry.seed_ini({ini_id});

  const auto rec = registry.find_by_identity_key(ini_id.serialize());
  ASSERT_TRUE(rec.has_value());
  ASSERT_FALSE(rec->online());
  ASSERT_TRUE(rec->source == device_source_e::ini);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_compute_device_identity_alsa_seq),
      TEST(test_compute_device_identity_rawmidi),
      TEST(test_compute_device_identity_rtpmidi_client),
      TEST(test_compute_device_identity_rtpmidi_server_factory),
      TEST(test_compute_device_identity_rtpmidi_multi),
      TEST(test_compute_device_identity_alsa_multi),
      TEST(test_registry_peer_appears_online),
      TEST(test_registry_peer_removed_offline_still_listed),
      TEST(test_registry_merge_ini_manual_discovered),
      TEST(test_registry_ini_seed_without_peer),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

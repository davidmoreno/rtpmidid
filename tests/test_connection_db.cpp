/**
 * Phase 5: connection_db v2 and directed restore tests.
 */
#include "../src/connection_db.hpp"
#include "../src/connection_restore.hpp"
#include "../src/device_identity.hpp"
#include "../src/factory.hpp"
#include "../src/midipeer.hpp"
#include "../src/midirouter.hpp"
#include "../src/peer_kind.hpp"
#include "test_case.hpp"
#include <memory>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <string>
#include <vector>

using namespace rtpmididns;

namespace rtpmididns {
std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
} // namespace rtpmididns

namespace {

class test_midiio_t : public midipeer_t {
public:
  explicit test_midiio_t(std::string name, std::string client,
                         std::string port)
      : name_(std::move(name)), client_(std::move(client)),
        port_(std::move(port)) {}

  void send_midi(midipeer_id_t /*from*/, const mididata_t &) override {}
  const char *get_type() const override {
    return peer_kind_wire_type(peer_kind_e::device_alsa_seq);
  }
  router_peer_row_t status() const override {
    router_peer_row_t row;
    row.type = get_type();
    row.name = name_;
    alsa_subscribe_from_t sub;
    sub.client_name = client_;
    sub.port_name = port_;
    row.alsa_subscribe_from = sub;
    return row;
  }

private:
  std::string name_;
  std::string client_;
  std::string port_;
};

online_device_t device_for(const std::string &client, const std::string &port,
                           peer_id_t peer_id) {
  online_device_t out;
  out.peer_id = peer_id;
  out.identity =
      *device_identity_t::parse("alsa_seq:client=" + client + ",port=" + port);
  return out;
}

bool has_edge(const std::vector<connect_action_t> &actions, peer_id_t from,
              peer_id_t to) {
  for (const auto &a : actions) {
    if (a.from == from && a.to == to)
      return true;
  }
  return false;
}

} // namespace

void test_connection_db_save_list_roundtrip() {
  connection_db_t db(":memory:");
  ASSERT_TRUE(db.is_open());

  stored_connection_t row;
  row.side_a = "alsa_seq:client=Peak,port=In";
  row.side_b = "rawmidi:device=/dev/snd/midiC0D0,name=Export";
  row.direction = connection_direction_e::a2b;
  row.enabled = true;
  db.save_connection(row);

  const auto listed = db.list_connections();
  ASSERT_EQUAL(listed.size(), size_t{1});
  ASSERT_EQUAL(listed[0].side_a, row.side_a);
  ASSERT_EQUAL(listed[0].side_b, row.side_b);
  ASSERT_TRUE(listed[0].direction == connection_direction_e::a2b);
  ASSERT_TRUE(listed[0].enabled);
}

void test_connection_restore_a2b_only() {
  const std::vector online = {device_for("Peak", "In", 1),
                              device_for("Peak", "Out", 2)};
  stored_connection_t conn;
  conn.side_a = "alsa_seq:client=Peak,port=In";
  conn.side_b = "alsa_seq:client=Peak,port=Out";
  conn.direction = connection_direction_e::a2b;
  conn.enabled = true;

  const auto actions =
      plan_connection_restore({conn}, online, [](peer_id_t, peer_id_t) {
        return false;
      });
  ASSERT_EQUAL(actions.size(), size_t{1});
  ASSERT_TRUE(has_edge(actions, 1, 2));
  ASSERT_FALSE(has_edge(actions, 2, 1));
}

void test_connection_restore_b2a_only() {
  const std::vector online = {device_for("Peak", "In", 1),
                              device_for("Peak", "Out", 2)};
  stored_connection_t conn;
  conn.side_a = "alsa_seq:client=Peak,port=In";
  conn.side_b = "alsa_seq:client=Peak,port=Out";
  conn.direction = connection_direction_e::b2a;
  conn.enabled = true;

  const auto actions =
      plan_connection_restore({conn}, online, [](peer_id_t, peer_id_t) {
        return false;
      });
  ASSERT_EQUAL(actions.size(), size_t{1});
  ASSERT_TRUE(has_edge(actions, 2, 1));
  ASSERT_FALSE(has_edge(actions, 1, 2));
}

void test_connection_restore_both_directions() {
  const std::vector online = {device_for("Peak", "In", 1),
                              device_for("Peak", "Out", 2)};
  stored_connection_t conn;
  conn.side_a = "alsa_seq:client=Peak,port=In";
  conn.side_b = "alsa_seq:client=Peak,port=Out";
  conn.direction = connection_direction_e::both;
  conn.enabled = true;

  const auto actions =
      plan_connection_restore({conn}, online, [](peer_id_t, peer_id_t) {
        return false;
      });
  ASSERT_EQUAL(actions.size(), size_t{2});
  ASSERT_TRUE(has_edge(actions, 1, 2));
  ASSERT_TRUE(has_edge(actions, 2, 1));
}

void test_connection_restore_query_fanout() {
  online_device_t in1 = device_for("Peak", "In", 1);
  online_device_t in2 = device_for("Peak", "In", 3);
  in2.identity = *device_identity_t::parse("alsa_seq:client=Peak,port=In");
  online_device_t out = device_for("Peak", "Out", 2);
  const std::vector online = {in1, in2, out};

  stored_connection_t conn;
  conn.side_a = "alsa_seq:client=Peak,port=In";
  conn.side_b = "alsa_seq:client=Peak,port=Out";
  conn.direction = connection_direction_e::a2b;
  conn.enabled = true;

  const auto actions =
      plan_connection_restore({conn}, online, [](peer_id_t, peer_id_t) {
        return false;
      });
  ASSERT_EQUAL(actions.size(), size_t{2});
  ASSERT_TRUE(has_edge(actions, 1, 2));
  ASSERT_TRUE(has_edge(actions, 3, 2));
}

void test_connection_restore_disabled_skipped() {
  const std::vector online = {device_for("Peak", "In", 1),
                              device_for("Peak", "Out", 2)};
  stored_connection_t conn;
  conn.side_a = "alsa_seq:client=Peak,port=In";
  conn.side_b = "alsa_seq:client=Peak,port=Out";
  conn.direction = connection_direction_e::both;
  conn.enabled = false;

  const auto actions = plan_connection_restore({conn}, online, nullptr);
  ASSERT_EQUAL(actions.size(), size_t{0});
}

void test_connection_enable_disable_roundtrip() {
  connection_db_t db(":memory:");
  stored_connection_t row;
  row.side_a = "alsa_seq:client=Peak,port=In";
  row.side_b = "alsa_seq:client=Peak,port=Out";
  row.direction = connection_direction_e::a2b;
  row.enabled = true;
  db.save_connection(row);

  ASSERT_TRUE(db.set_enabled(row.side_a, row.side_b, false));
  auto listed = db.list_connections();
  ASSERT_EQUAL(listed.size(), size_t{1});
  ASSERT_FALSE(listed[0].enabled);

  ASSERT_TRUE(db.set_enabled(row.side_a, row.side_b, true));
  listed = db.list_connections();
  ASSERT_TRUE(listed[0].enabled);
}

void test_connection_manager_applies_directed_restore() {
  auto router = std::make_shared<midirouter_t>();
  auto db = std::make_unique<connection_db_t>(":memory:");
  connection_db_manager_t manager(router, std::move(db));
  manager.attach();

  stored_connection_t conn;
  conn.side_a = "alsa_seq:client=Peak,port=In";
  conn.side_b = "alsa_seq:client=Peak,port=Out";
  conn.direction = connection_direction_e::a2b;
  conn.enabled = true;
  manager.database().save_connection(conn);

  auto in_peer = std::make_shared<test_midiio_t>("in", "Peak", "In");
  auto out_peer = std::make_shared<test_midiio_t>("out", "Peak", "Out");
  const auto in_id = router->add_peer(in_peer);
  const auto out_id = router->add_peer(out_peer);

  manager.check_reconnects_for_all();

  const auto in_targets = router->send_targets_for(in_id);
  const auto out_targets = router->send_targets_for(out_id);
  ASSERT_EQUAL(in_targets.size(), size_t{1});
  ASSERT_EQUAL(in_targets[0], out_id);
  ASSERT_EQUAL(out_targets.size(), size_t{0});
}

void test_canonicalize_stored_connection_flips_direction() {
  stored_connection_t row;
  row.side_a = "z_side";
  row.side_b = "a_side";
  row.direction = connection_direction_e::a2b;
  const auto canon = canonicalize_stored_connection(row);
  ASSERT_EQUAL(canon.side_a, "a_side");
  ASSERT_EQUAL(canon.side_b, "z_side");
  ASSERT_TRUE(canon.direction == connection_direction_e::b2a);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_connection_db_save_list_roundtrip),
      TEST(test_canonicalize_stored_connection_flips_direction),
      TEST(test_connection_restore_a2b_only),
      TEST(test_connection_restore_b2a_only),
      TEST(test_connection_restore_both_directions),
      TEST(test_connection_restore_query_fanout),
      TEST(test_connection_restore_disabled_skipped),
      TEST(test_connection_enable_disable_roundtrip),
      TEST(test_connection_manager_applies_directed_restore),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}

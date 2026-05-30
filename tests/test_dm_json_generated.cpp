/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "./test_case.hpp"
#include "dm_json_generated.hpp"
#include "dm_json_rpc.hpp"
#include <string>

static void test_router_remove_roundtrip() {
  rtpmididns::router_remove_params_t p{};
  p.peer_id = 42;
  const std::string j = rtpmididns::dmjson::to_json(p);
  rtpmididns::router_remove_params_t o{};
  ASSERT_TRUE(rtpmididns::dmjson::from_json(j, o));
  ASSERT_EQUAL(o.peer_id, 42ull);
}

static void test_connect_params_optional_omit() {
  rtpmididns::connect_params_t p{};
  p.hostname = "10.0.0.1";
  const std::string j = rtpmididns::dmjson::to_json(p);
  ASSERT_NOT_EQUAL(j.find("10.0.0.1"), std::string::npos);
  rtpmididns::connect_params_t o{};
  ASSERT_TRUE(rtpmididns::dmjson::from_json(j, o));
  ASSERT_EQUAL(o.hostname, "10.0.0.1");
  ASSERT_FALSE(o.port.has_value());
}

static void test_connections_save_params_roundtrip() {
  rtpmididns::connections_save_params_t p{};
  p.side_a = "rtpmidi_server:name=Peak";
  p.side_b = "alsa_seq:client=Peak,port=In";
  p.direction = "a2b";
  p.enabled = 1;
  const std::string j = rtpmididns::dmjson::to_json(p);
  rtpmididns::connections_save_params_t o{};
  ASSERT_TRUE(rtpmididns::dmjson::from_json(j, o));
  ASSERT_EQUAL(o.side_a, p.side_a);
  ASSERT_EQUAL(o.direction, "a2b");
}

static void test_devices_list_row_roundtrip() {
  rtpmididns::device_list_row_t row{};
  row.identity = "rawmidi:device=/dev/snd/midiC0D0";
  row.type = "rawmidi";
  row.name = "Export";
  row.source = "manual";
  row.online = 1;
  row.peer_id = 7;
  const std::string j = rtpmididns::dmjson::to_json(row);
  rtpmididns::device_list_row_t o{};
  ASSERT_TRUE(rtpmididns::dmjson::from_json(j, o));
  ASSERT_EQUAL(o.identity, row.identity);
  ASSERT_TRUE(o.peer_id.has_value());
  ASSERT_EQUAL(*o.peer_id, 7ull);
}

int main(int argc, char **argv) {
  test_case_t testcase{TEST(test_router_remove_roundtrip),
                       TEST(test_connect_params_optional_omit),
                       TEST(test_connections_save_params_roundtrip),
                       TEST(test_devices_list_row_roundtrip)};
  testcase.run(argc, argv);
  return testcase.exit_code();
}

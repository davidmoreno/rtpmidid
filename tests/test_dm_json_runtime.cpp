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
#include <rtpmidid/dm_json/runtime.hpp>
#include <string>

using rtpmididns::dmjson::writer_t;

static void test_writer_object_and_string_escape() {
  writer_t w;
  w.begin_object();
  w.key("a");
  w.bool_value(true);
  w.key("b");
  w.string_value("line1\n\"quotes\"");
  w.end_object();
  std::string s;
  w.swap_into_string(s);
  ASSERT_EQUAL(s, R"({"a":true,"b":"line1\n\"quotes\""})");
}

static void test_writer_array_commas() {
  writer_t w;
  w.begin_array();
  w.array_item();
  w.int_value(1);
  w.array_item();
  w.int_value(2);
  w.end_array();
  std::string s;
  w.swap_into_string(s);
  ASSERT_EQUAL(s, "[1,2]");
}

static void test_scan_envelope_basic() {
  rtpmididns::dmjson::rpc::envelope_info_t env;
  std::string err;
  const std::string buf = R"({"params":{"x":1},"method":"status","id":3})";
  ASSERT_TRUE(rtpmididns::dmjson::rpc::scan_envelope(buf, env, err));
  ASSERT_EQUAL(env.method, "status");
  ASSERT_TRUE(env.has_params);
  ASSERT_EQUAL(std::string(env.params_json), R"({"x":1})");
  ASSERT_TRUE(env.has_id);
}

static void test_scan_envelope_no_params() {
  rtpmididns::dmjson::rpc::envelope_info_t env;
  std::string err;
  const std::string buf = R"({"method":"help"})";
  ASSERT_TRUE(rtpmididns::dmjson::rpc::scan_envelope(buf, env, err));
  ASSERT_EQUAL(env.method, "help");
  ASSERT_FALSE(env.has_params);
}

static void test_read_bool() {
  rtpmididns::dmjson::reader_t r("  true ");
  ASSERT_TRUE(r.read_bool());
}

int main(int argc, char **argv) {
  test_case_t testcase{TEST(test_writer_object_and_string_escape),
                       TEST(test_writer_array_commas),
                       TEST(test_scan_envelope_basic),
                       TEST(test_scan_envelope_no_params),
                       TEST(test_read_bool)};
  testcase.run(argc, argv);
  return testcase.exit_code();
}

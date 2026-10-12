/**
 * rtpmidid-lite: one ALSA sequencer port exported over RTP-MIDI.
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include "lite/config.hpp"
#include "lite/sysex.hpp"
#include "test_case.hpp"

#include <cstdint>
#include <string>
#include <vector>

//
// Argument parsing
//

void test_parse_endpoint_host_only(void) {
  auto endpoint = rtpmidid_lite::parse_endpoint("192.168.1.10");
  ASSERT_EQUAL(endpoint.host, std::string("192.168.1.10"));
  ASSERT_EQUAL(endpoint.port, std::string("5004"));
}

void test_parse_endpoint_host_port(void) {
  auto endpoint = rtpmidid_lite::parse_endpoint("10.0.0.1:6000");
  ASSERT_EQUAL(endpoint.host, std::string("10.0.0.1"));
  ASSERT_EQUAL(endpoint.port, std::string("6000"));
}

void test_parse_endpoint_ipv6(void) {
  auto endpoint = rtpmidid_lite::parse_endpoint("[fe80::1]:7000", "1234");
  ASSERT_EQUAL(endpoint.host, std::string("fe80::1"));
  ASSERT_EQUAL(endpoint.port, std::string("7000"));

  auto bare = rtpmidid_lite::parse_endpoint("fe80::1", "1234");
  ASSERT_EQUAL(bare.host, std::string("fe80::1"));
  ASSERT_EQUAL(bare.port, std::string("1234"));
}

void test_parse_args_run(void) {
  rtpmidid_lite::config_t config;
  std::vector<std::string> args{
      "--name",        "synth",     "--port",   "6000",        "--connect",
      "10.0.0.1:7000", "--connect", "10.0.0.2", "--log-level", "debug"};

  const auto status = rtpmidid_lite::parse_args(args, &config);

  ASSERT_TRUE(status == rtpmidid_lite::parse_status_e::RUN);
  ASSERT_EQUAL(config.name, std::string("synth"));
  ASSERT_EQUAL(config.port, std::string("6000"));
  ASSERT_EQUAL(config.connect_to.size(), size_t(2));
  ASSERT_EQUAL(config.connect_to[0].host, std::string("10.0.0.1"));
  ASSERT_EQUAL(config.connect_to[0].port, std::string("7000"));
  ASSERT_EQUAL(config.connect_to[1].host, std::string("10.0.0.2"));
  ASSERT_EQUAL(config.connect_to[1].port, std::string("5004"));
  ASSERT_TRUE(config.log_level == rtpmidid::logger_level_t::DEBUG);
}

void test_parse_args_default_name_is_hostname(void) {
  rtpmidid_lite::config_t config;
  const auto status = rtpmidid_lite::parse_args({}, &config);

  ASSERT_TRUE(status == rtpmidid_lite::parse_status_e::RUN);
  ASSERT_FALSE(rtpmidid_lite::hostname().empty());
  ASSERT_EQUAL(config.name, rtpmidid_lite::hostname());
}

void test_parse_args_list(void) {
  rtpmidid_lite::config_t config;
  std::vector<std::string> args{"--list", "--list-timeout", "1.5"};

  const auto status = rtpmidid_lite::parse_args(args, &config);

  ASSERT_TRUE(status == rtpmidid_lite::parse_status_e::LIST);
  ASSERT_EQUAL(config.list_timeout.count(), int64_t(1500));
}

void test_parse_args_errors(void) {
  rtpmidid_lite::config_t config;

  ASSERT_TRUE(rtpmidid_lite::parse_args({"--nope"}, &config) ==
              rtpmidid_lite::parse_status_e::EXIT_ERROR);
  ASSERT_TRUE(rtpmidid_lite::parse_args({"--port", "abc"}, &config) ==
              rtpmidid_lite::parse_status_e::EXIT_ERROR);
  ASSERT_TRUE(rtpmidid_lite::parse_args({"--connect"}, &config) ==
              rtpmidid_lite::parse_status_e::EXIT_ERROR);
}

//
// SysEx segmentation
//

std::vector<std::vector<uint8_t>> segments_of(const std::vector<uint8_t> &input,
                                              size_t segment_bytes) {
  std::vector<std::vector<uint8_t>> out;
  rtpmidid_lite::split_sysex(input.data(), input.size(), segment_bytes,
                             [&out](const uint8_t *data, size_t len) {
                               out.emplace_back(data, data + len);
                             });
  return out;
}

std::vector<uint8_t> payload_of(const std::vector<std::vector<uint8_t>> &segs) {
  std::vector<uint8_t> payload;
  for (const auto &segment : segs) {
    payload.insert(payload.end(), segment.begin() + 1, segment.end() - 1);
  }
  return payload;
}

void test_sysex_short_is_verbatim(void) {
  const std::vector<uint8_t> sysex{0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7};

  const auto segments = segments_of(sysex, 256);

  ASSERT_EQUAL(segments.size(), size_t(1));
  ASSERT_TRUE(segments[0] == sysex);
}

void test_sysex_is_segmented(void) {
  std::vector<uint8_t> sysex{0xF0};
  for (int i = 0; i < 600; i++)
    sysex.push_back(static_cast<uint8_t>(i & 0x7F));
  sysex.push_back(0xF7);

  const auto segments = segments_of(sysex, 256);

  ASSERT_EQUAL(segments.size(), size_t(3));
  // First: F0 <256> F0, middle: F7 <256> F0, last: F7 <88> F7
  ASSERT_EQUAL(segments[0].front(), uint8_t(0xF0));
  ASSERT_EQUAL(segments[0].size(), size_t(258));
  ASSERT_EQUAL(segments[0].back(), uint8_t(0xF0));
  ASSERT_EQUAL(segments[1].front(), uint8_t(0xF7));
  ASSERT_EQUAL(segments[1].back(), uint8_t(0xF0));
  ASSERT_EQUAL(segments[2].front(), uint8_t(0xF7));
  ASSERT_EQUAL(segments[2].size(), size_t(90));
  ASSERT_EQUAL(segments[2].back(), uint8_t(0xF7));

  const std::vector<uint8_t> expected(sysex.begin() + 1, sysex.end() - 1);
  ASSERT_TRUE(payload_of(segments) == expected);
}

void test_sysex_rejects_malformed(void) {
  const std::vector<uint8_t> not_sysex{0x90, 0x40, 0x7F};
  ASSERT_EQUAL(segments_of(not_sysex, 256).size(), size_t(0));

  const std::vector<uint8_t> unterminated{0xF0, 0x01, 0x02};
  ASSERT_EQUAL(segments_of(unterminated, 256).size(), size_t(0));
}

//
// SysEx reassembly from the raw fragments ALSA produces for long messages
//

using sysex_result = rtpmidid_lite::sysex_feed_result_e;

std::vector<uint8_t> long_sysex(void) {
  std::vector<uint8_t> message{0xF0};
  for (int i = 0; i < 600; i++)
    message.push_back(static_cast<uint8_t>(i & 0x7F));
  message.push_back(0xF7);
  return message;
}

void test_sysex_assembler_single(void) {
  rtpmidid_lite::sysex_assembler_t assembler;
  std::vector<std::vector<uint8_t>> emitted;
  const auto collect = [&emitted](const uint8_t *data, size_t len) {
    emitted.emplace_back(data, data + len);
  };

  const std::vector<uint8_t> message{0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7};
  ASSERT_TRUE(assembler.feed(message.data(), message.size(), collect) ==
              sysex_result::COMPLETE);
  ASSERT_EQUAL(emitted.size(), size_t(1));
  ASSERT_TRUE(emitted[0] == message);
}

void test_sysex_assembler_fragments(void) {
  // The kernel splits a message this long into 256 + 256 + 90 raw fragments.
  const auto message = long_sysex();
  ASSERT_EQUAL(message.size(), size_t(602));

  rtpmidid_lite::sysex_assembler_t assembler;
  std::vector<std::vector<uint8_t>> emitted;
  const auto collect = [&emitted](const uint8_t *data, size_t len) {
    emitted.emplace_back(data, data + len);
  };

  ASSERT_TRUE(assembler.feed(message.data(), 256, collect) ==
              sysex_result::STORED);
  ASSERT_TRUE(assembler.feed(message.data() + 256, 256, collect) ==
              sysex_result::STORED);
  ASSERT_TRUE(assembler.feed(message.data() + 512, 90, collect) ==
              sysex_result::COMPLETE);

  ASSERT_EQUAL(emitted.size(), size_t(1));
  ASSERT_TRUE(emitted[0] == message);
}

void test_sysex_assembler_restart(void) {
  rtpmidid_lite::sysex_assembler_t assembler;
  std::vector<std::vector<uint8_t>> emitted;
  const auto collect = [&emitted](const uint8_t *data, size_t len) {
    emitted.emplace_back(data, data + len);
  };

  const std::vector<uint8_t> aborted{0xF0, 0x01, 0x02};
  const std::vector<uint8_t> message{0xF0, 0x10, 0xF7};

  ASSERT_TRUE(assembler.feed(aborted.data(), aborted.size(), collect) ==
              sysex_result::STORED);
  ASSERT_TRUE(assembler.feed(message.data(), message.size(), collect) ==
              sysex_result::COMPLETE);

  ASSERT_EQUAL(emitted.size(), size_t(1));
  ASSERT_TRUE(emitted[0] == message);
}

void test_sysex_assembler_stray(void) {
  rtpmidid_lite::sysex_assembler_t assembler;
  size_t emitted = 0;
  const auto collect = [&emitted](const uint8_t *, size_t) { emitted++; };

  const std::vector<uint8_t> continuation{0x01, 0x02, 0x03};
  ASSERT_TRUE(assembler.feed(continuation.data(), continuation.size(),
                             collect) == sysex_result::DISCARDED);
  ASSERT_EQUAL(emitted, size_t(0));
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_parse_endpoint_host_only),
      TEST(test_parse_endpoint_host_port),
      TEST(test_parse_endpoint_ipv6),
      TEST(test_parse_args_run),
      TEST(test_parse_args_default_name_is_hostname),
      TEST(test_parse_args_list),
      TEST(test_parse_args_errors),
      TEST(test_sysex_short_is_verbatim),
      TEST(test_sysex_is_segmented),
      TEST(test_sysex_rejects_malformed),
      TEST(test_sysex_assembler_single),
      TEST(test_sysex_assembler_fragments),
      TEST(test_sysex_assembler_restart),
      TEST(test_sysex_assembler_stray),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

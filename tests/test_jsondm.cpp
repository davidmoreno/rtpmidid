/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * Unit tests for the json-dm runtime (include/rtpmidid/jsondm.hpp).
 * The serializer/deserializer specializations in this file are hand-written
 * stand-ins for what scripts/json_dm_to_cpp.py will generate; they must be
 * kept in sync with the generator's output shape.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "./test_case.hpp"
#include "control_status_jsondm.hpp"
#include "peer_status_jsondm.hpp"
#include <rtpmidid/jsondm.hpp>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Fixture structs (would be /// [JSON-DM] decorated in real headers)
// ---------------------------------------------------------------------------

struct jsondm_inner_t {
  std::string name;
  int port;
};
struct jsondm_outer_t {
  int id;
  std::string name;
  bool ok;
  double ratio;
  float gain;
  uint64_t big;
  std::vector<int> items;
  std::vector<std::string> tags;
  std::unordered_map<std::string, jsondm_inner_t> peers;
  std::optional<std::string> nick;
  std::optional<int> missing;
  std::variant<std::monostate, int, std::string> choice;
  jsondm_inner_t inner;
  std::vector<jsondm_inner_t> inlist;
};
struct jsondm_array_mode_t { // positional-array mode (/// [JSON-DM-ARRAY])
  std::string name;
  std::string hostname;
  int port;
};
struct jsondm_rec_t { // recursion via vector (vector<T> allows incomplete T)
  std::vector<jsondm_rec_t> next;
};
using jsondm_ambig_variant_t =
    std::variant<jsondm_inner_t, jsondm_array_mode_t>;
using jsondm_nostr_variant_t = std::variant<std::monostate, int>;

namespace jsondm {
template <> struct serializer<jsondm_inner_t> {
  static void write(const jsondm_inner_t &v, Writer &w) {
    w.obj_begin();
    {
      Writer::member_guard g(w, "name");
      jsondm::write(w, v.name);
    }
    {
      Writer::member_guard g(w, "port");
      jsondm::write(w, v.port);
    }
    w.obj_end();
  }
};
template <> struct deserializer<jsondm_inner_t> {
  static void read(Reader &r, jsondm_inner_t &v) {
    r.obj_begin();
    while (r.next_key()) {
      auto key = r.key();
      if (key == "name") {
        Reader::member_guard g(r, "name");
        jsondm::read(r, v.name);
      } else if (key == "port") {
        Reader::member_guard g(r, "port");
        jsondm::read(r, v.port);
      } else
        r.skip_value();
    }
    r.obj_end();
  }
};
template <> struct serializer<jsondm_outer_t> {
  static void write(const jsondm_outer_t &v, Writer &w) {
    w.obj_begin();
    {
      Writer::member_guard g(w, "id");
      jsondm::write(w, v.id);
    }
    {
      Writer::member_guard g(w, "name");
      jsondm::write(w, v.name);
    }
    {
      Writer::member_guard g(w, "ok");
      jsondm::write(w, v.ok);
    }
    {
      Writer::member_guard g(w, "ratio");
      jsondm::write(w, v.ratio);
    }
    {
      Writer::member_guard g(w, "gain");
      jsondm::write(w, v.gain);
    }
    {
      Writer::member_guard g(w, "big");
      jsondm::write(w, v.big);
    }
    {
      Writer::member_guard g(w, "items");
      jsondm::write(w, v.items);
    }
    {
      Writer::member_guard g(w, "tags");
      jsondm::write(w, v.tags);
    }
    {
      Writer::member_guard g(w, "peers");
      jsondm::write(w, v.peers);
    }
    if (v.nick) {
      Writer::member_guard g(w, "nick");
      jsondm::write(w, *v.nick);
    }
    if (v.missing) {
      Writer::member_guard g(w, "missing");
      jsondm::write(w, *v.missing);
    }
    {
      Writer::member_guard g(w, "choice");
      jsondm::write(w, v.choice);
    }
    {
      Writer::member_guard g(w, "inner");
      jsondm::write(w, v.inner);
    }
    {
      Writer::member_guard g(w, "inlist");
      jsondm::write(w, v.inlist);
    }
    w.obj_end();
  }
};
template <> struct deserializer<jsondm_outer_t> {
  static void read(Reader &r, jsondm_outer_t &v) {
    r.obj_begin();
    while (r.next_key()) {
      auto key = r.key();
      if (key == "id") {
        Reader::member_guard g(r, "id");
        jsondm::read(r, v.id);
      } else if (key == "name") {
        Reader::member_guard g(r, "name");
        jsondm::read(r, v.name);
      } else if (key == "ok") {
        Reader::member_guard g(r, "ok");
        jsondm::read(r, v.ok);
      } else if (key == "ratio") {
        Reader::member_guard g(r, "ratio");
        jsondm::read(r, v.ratio);
      } else if (key == "gain") {
        Reader::member_guard g(r, "gain");
        jsondm::read(r, v.gain);
      } else if (key == "big") {
        Reader::member_guard g(r, "big");
        jsondm::read(r, v.big);
      } else if (key == "items") {
        Reader::member_guard g(r, "items");
        jsondm::read(r, v.items);
      } else if (key == "tags") {
        Reader::member_guard g(r, "tags");
        jsondm::read(r, v.tags);
      } else if (key == "peers") {
        Reader::member_guard g(r, "peers");
        jsondm::read(r, v.peers);
      } else if (key == "nick") {
        Reader::member_guard g(r, "nick");
        jsondm::read(r, v.nick);
      } else if (key == "missing") {
        Reader::member_guard g(r, "missing");
        jsondm::read(r, v.missing);
      } else if (key == "choice") {
        Reader::member_guard g(r, "choice");
        jsondm::read(r, v.choice);
      } else if (key == "inner") {
        Reader::member_guard g(r, "inner");
        jsondm::read(r, v.inner);
      } else if (key == "inlist") {
        Reader::member_guard g(r, "inlist");
        jsondm::read(r, v.inlist);
      } else
        r.skip_value();
    }
    r.obj_end();
  }
};
template <> struct serializer<jsondm_array_mode_t> {
  static void write(const jsondm_array_mode_t &v, Writer &w) {
    w.arr_begin();
    {
      Writer::member_guard g(w, 0);
      jsondm::write(w, v.name);
    }
    {
      Writer::member_guard g(w, 1);
      jsondm::write(w, v.hostname);
    }
    {
      Writer::member_guard g(w, 2);
      jsondm::write(w, v.port);
    }
    w.arr_end();
  }
};
template <> struct deserializer<jsondm_array_mode_t> {
  static void read(Reader &r, jsondm_array_mode_t &v) {
    r.arr_begin();
    size_t i = 0;
    while (r.next_elem()) {
      Reader::member_guard g(r, i);
      if (i == 0)
        jsondm::read(r, v.name);
      else if (i == 1)
        jsondm::read(r, v.hostname);
      else if (i == 2)
        jsondm::read(r, v.port);
      else
        r.skip_value();
      ++i;
    }
    r.arr_end();
    if (i != 3) {
      r.fail("3 array elements", FMT::format("{} elements", i));
    }
  }
};
template <> struct serializer<jsondm_rec_t> {
  static void write(const jsondm_rec_t &v, Writer &w) {
    w.obj_begin();
    {
      Writer::member_guard g(w, "next");
      jsondm::write(w, v.next);
    }
    w.obj_end();
  }
};
template <> struct deserializer<jsondm_rec_t> {
  static void read(Reader &r, jsondm_rec_t &v) {
    r.obj_begin();
    while (r.next_key()) {
      auto key = r.key();
      if (key == "next") {
        Reader::member_guard g(r, "next");
        jsondm::read(r, v.next);
      } else
        r.skip_value();
    }
    r.obj_end();
  }
};
} // namespace jsondm
template <>
struct FMT::formatter<jsondm_outer_t> : jsondm::formatter_base<jsondm_outer_t> {
};
template <>
struct FMT::formatter<jsondm_inner_t> : jsondm::formatter_base<jsondm_inner_t> {
};
template <>
struct FMT::formatter<jsondm_array_mode_t>
    : jsondm::formatter_base<jsondm_array_mode_t> {};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static jsondm_outer_t make_outer() {
  jsondm_outer_t v;
  v.id = 42;
  v.name = "héllo \"world\"\n";
  v.ok = true;
  v.ratio = 0.1;
  v.gain = 2.5f;
  v.big = 0xFFFFFFFFFFFFFFFFull;
  v.items = {1, 2, 3};
  v.tags = {"a", "b\"c"};
  v.peers["p1"] = jsondm_inner_t{"one", 111};
  v.peers["p2"] = jsondm_inner_t{"two", 222};
  v.nick = std::string("x");
  v.missing = std::nullopt;
  v.choice = std::string("pick");
  v.inner = jsondm_inner_t{"in", 9};
  v.inlist = {jsondm_inner_t{"a", 1}, jsondm_inner_t{"b", 2}};
  return v;
}

template <typename T> static bool throws(std::string_view json) {
  try {
    T v;
    jsondm::deserialize(json, v);
    return false;
  } catch (const jsondm::exception &) {
    return true;
  }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_roundtrip() {
  auto v = make_outer();
  std::string out;
  jsondm::serialize(v, out);
  jsondm_outer_t v2;
  jsondm::deserialize(out, v2);
  ASSERT_EQUAL(v2.id, 42);
  ASSERT_EQUAL(v2.name, v.name);
  ASSERT_TRUE(v2.ok);
  ASSERT_TRUE(v2.ratio == v.ratio);
  ASSERT_TRUE(v2.gain == v.gain);
  ASSERT_EQUAL(v2.big, 0xFFFFFFFFFFFFFFFFull);
  ASSERT_EQUAL(v2.items.size(), v.items.size());
  ASSERT_EQUAL(v2.items[0], 1);
  ASSERT_EQUAL(v2.items[2], 3);
  ASSERT_EQUAL(v2.tags.size(), v.tags.size());
  ASSERT_EQUAL(v2.tags[1], "b\"c");
  ASSERT_EQUAL(v2.peers.size(), (size_t)2);
  ASSERT_EQUAL(v2.peers.at("p1").port, 111);
  ASSERT_TRUE(v2.nick.has_value() && *v2.nick == "x");
  ASSERT_FALSE(v2.missing.has_value());
  ASSERT_TRUE(std::get<std::string>(v2.choice) == "pick");
  ASSERT_EQUAL(v2.inner.port, 9);
  ASSERT_EQUAL(v2.inlist.size(), (size_t)2);
  ASSERT_EQUAL(v2.inlist[1].name, "b");
  // optional omission + presence
  ASSERT_TRUE(out.find("\"missing\"") == std::string::npos);
  ASSERT_TRUE(out.find("\"nick\"") != std::string::npos);
  // uint64 max serialized exactly
  ASSERT_TRUE(out.find("18446744073709551615") != std::string::npos);
}

void test_scalars() {
  // float shortest round-trip
  std::string out;
  double d = 1.0 / 3.0;
  jsondm::serialize(d, out);
  ASSERT_EQUAL(out, "0.3333333333333333");
  double back = 0;
  jsondm::deserialize(out, back);
  ASSERT_TRUE(back == d);
  // int
  int n = -12345;
  out.clear();
  jsondm::serialize(n, out);
  ASSERT_EQUAL(out, "-12345");
  int n2 = 0;
  jsondm::deserialize(out, n2);
  ASSERT_EQUAL(n2, n);
  // malformed numbers
  ASSERT_TRUE(throws<int>("12abc"));
  ASSERT_TRUE(throws<int>("\"x\""));
  ASSERT_TRUE(throws<int>("1e999"));
  ASSERT_TRUE(throws<int>(""));
  // float out of range
  ASSERT_TRUE(throws<double>("1e999"));
  // integer with fraction rejected
  ASSERT_TRUE(throws<int>("1.5"));
  ASSERT_TRUE(throws<int>("1e2"));
}

void test_strings() {
  std::string out;
  jsondm::serialize(std::string("a\"b\\c\nd\te"), out);
  ASSERT_EQUAL(out, "\"a\\\"b\\\\c\\nd\\te\"");
  std::string back;
  jsondm::deserialize(out, back);
  ASSERT_EQUAL(back, "a\"b\\c\nd\te");
  // surrogate pair
  jsondm::deserialize(R"("\ud83d\ude00")", back);
  ASSERT_EQUAL(back, "\xF0\x9F\x98\x80");
  // BMP
  jsondm::deserialize(R"("\u00e9")", back);
  ASSERT_EQUAL(back, "\xC3\xA9");
  // invalid escapes
  ASSERT_TRUE(throws<std::string>(R"("\x")"));
  ASSERT_TRUE(throws<std::string>(R"("\u12")"));
  ASSERT_TRUE(throws<std::string>("\"unterminated"));
  // lone surrogates
  ASSERT_TRUE(throws<std::string>(R"("\ud800")"));
  ASSERT_TRUE(throws<std::string>(R"("\udc00")"));
}

void test_containers() {
  std::vector<int> v{1, 2, 3};
  std::string out;
  jsondm::serialize(v, out);
  ASSERT_EQUAL(out, "[1,2,3]");
  std::vector<int> back;
  jsondm::deserialize(out, back);
  ASSERT_EQUAL(back.size(), v.size());
  ASSERT_EQUAL(back[0], 1);
  ASSERT_EQUAL(back[2], 3);
  // empty
  std::vector<int> e;
  out.clear();
  jsondm::serialize(e, out);
  ASSERT_EQUAL(out, "[]");
  jsondm::deserialize("[]", e);
  ASSERT_EQUAL(e.size(), (size_t)0);
  // map
  std::unordered_map<std::string, int> m{{"a", 1}, {"b", 2}};
  out.clear();
  jsondm::serialize(m, out);
  ASSERT_TRUE(out.find("\"a\":1") != std::string::npos);
  ASSERT_TRUE(out.find("\"b\":2") != std::string::npos);
  std::unordered_map<std::string, int> m2;
  jsondm::deserialize(out, m2);
  ASSERT_EQUAL(m2.size(), (size_t)2);
  ASSERT_EQUAL(m2.at("a"), 1);
  ASSERT_EQUAL(m2.at("b"), 2);
  // unknown keys skipped
  jsondm_outer_t o;
  jsondm::deserialize(R"({"id": 1, "future": {"deep": [1,2]}, "name": "z"})",
                      o);
  ASSERT_EQUAL(o.id, 1);
  ASSERT_EQUAL(o.name, "z");
}

void test_optional_variant() {
  // optional: null -> nullopt
  std::optional<int> o;
  jsondm::deserialize("null", o);
  ASSERT_FALSE(o.has_value());
  jsondm::deserialize("5", o);
  ASSERT_TRUE(o.has_value() && *o == 5);
  // optional in array -> null element
  std::vector<std::optional<int>> ov;
  jsondm::deserialize("[1,null,3]", ov);
  ASSERT_EQUAL(ov.size(), (size_t)3);
  ASSERT_TRUE(ov[1] == std::nullopt);
  // variant: monostate <-> null
  std::variant<std::monostate, int, std::string> v;
  jsondm::deserialize("null", v);
  ASSERT_TRUE(std::holds_alternative<std::monostate>(v));
  std::string out;
  jsondm::serialize(v, out);
  ASSERT_EQUAL(out, "null");
  // type dispatch
  jsondm::deserialize("7", v);
  ASSERT_TRUE(std::get<int>(v) == 7);
  jsondm::deserialize("\"hi\"", v);
  ASSERT_TRUE(std::get<std::string>(v) == "hi");
  // ambiguity
  ASSERT_TRUE(throws<jsondm_ambig_variant_t>("{}"));
  // no matching alternative
  ASSERT_TRUE(throws<jsondm_nostr_variant_t>("\"str\""));
}

void test_array_mode() {
  jsondm_array_mode_t p{"n", "h", 5004};
  std::string out;
  jsondm::serialize(p, out);
  ASSERT_EQUAL(out, "[\"n\",\"h\",5004]");
  jsondm_array_mode_t back;
  jsondm::deserialize(out, back);
  ASSERT_EQUAL(back.name, "n");
  ASSERT_EQUAL(back.hostname, "h");
  ASSERT_EQUAL(back.port, 5004);
  // wrong length
  ASSERT_TRUE(throws<jsondm_array_mode_t>("[\"a\",\"b\"]"));
  ASSERT_TRUE(throws<jsondm_array_mode_t>("[\"a\",\"b\",1,2]"));
  // type mismatch inside
  ASSERT_TRUE(throws<jsondm_array_mode_t>("[1,2,3]"));
}

void test_nested_and_depth() {
  // nested struct round-trip
  jsondm_outer_t o = make_outer();
  std::string out;
  jsondm::serialize(o, out);
  jsondm_outer_t back;
  jsondm::deserialize(out, back);
  ASSERT_EQUAL(back.inner.name, "in");
  ASSERT_EQUAL(back.inlist.size(), (size_t)2);
  // recursion round-trip
  jsondm_rec_t r;
  r.next.push_back(jsondm_rec_t{});
  r.next[0].next.push_back(jsondm_rec_t{});
  out.clear();
  jsondm::serialize(r, out);
  ASSERT_EQUAL(out, R"({"next":[{"next":[{"next":[]}]}]})");
  jsondm_rec_t rb;
  jsondm::deserialize(out, rb);
  ASSERT_EQUAL(rb.next.size(), (size_t)1);
  ASSERT_EQUAL(rb.next[0].next.size(), (size_t)1);
  // depth guard on write
  jsondm_rec_t deep;
  jsondm_rec_t *cur = &deep;
  for (int i = 0; i < 600; i++) {
    cur->next.push_back(jsondm_rec_t{});
    cur = &cur->next[0];
  }
  bool threw = false;
  try {
    jsondm::serialize(deep, out);
  } catch (const jsondm::exception &e) {
    threw = true;
    ASSERT_TRUE(std::string(e.what()).find("nesting exceeds") !=
                std::string::npos);
  }
  ASSERT_TRUE(threw);
  // depth guard on read
  std::string deep_json;
  for (int i = 0; i < 600; i++)
    deep_json += "{\"next\":[";
  deep_json += "{}";
  for (int i = 0; i < 600; i++)
    deep_json += "]}";
  threw = false;
  try {
    jsondm_rec_t v;
    jsondm::deserialize(deep_json, v);
  } catch (const jsondm::exception &) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

void test_errors() {
  // rich message: path + offset + expected/got + context
  try {
    jsondm_outer_t v;
    jsondm::deserialize(R"({"id": 1, "name": "x", "items": [1, "oops", 3]})",
                        v);
    ASSERT_FALSE(true);
  } catch (const jsondm::exception &e) {
    std::string msg = e.what();
    ASSERT_TRUE(msg.find("items[1]") != std::string::npos); // path
    ASSERT_TRUE(msg.find("offset") != std::string::npos);
    ASSERT_TRUE(msg.find("expected number") != std::string::npos);
    ASSERT_TRUE(msg.find("found string") != std::string::npos);
  }
  // serialize error path
  try {
    jsondm_outer_t v = make_outer();
    v.inlist[0].name = std::string(); // fine
    v.ratio = std::numeric_limits<double>::quiet_NaN();
    std::string out;
    jsondm::serialize(v, out);
    ASSERT_FALSE(true);
  } catch (const jsondm::exception &e) {
    ASSERT_TRUE(std::string(e.what()).find("ratio") != std::string::npos);
  }
  // infinity
  try {
    jsondm_outer_t v = make_outer();
    v.gain = std::numeric_limits<float>::infinity();
    std::string out;
    jsondm::serialize(v, out);
    ASSERT_FALSE(true);
  } catch (const jsondm::exception &) {
  }
  // trailing data
  ASSERT_TRUE(throws<jsondm_outer_t>(R"({"id":1} extra)"));
  // malformed json
  ASSERT_TRUE(throws<jsondm_outer_t>("{"));
  ASSERT_TRUE(throws<jsondm_outer_t>(R"({"id":})"));
  ASSERT_TRUE(throws<jsondm_outer_t>(R"({"id":1,)"));
}

void test_fmt_adapter() {
  auto v = make_outer();
  std::string out;
  FMT::format_to(std::back_inserter(out), "state: {}", v);
  ASSERT_TRUE(out.rfind("state: {", 0) == 0);
  // nested struct
  std::string out2 = FMT::format("{}", jsondm_inner_t{"n", 1});
  ASSERT_EQUAL(out2, R"({"name":"n","port":1})");
}

void test_fd_sink() {
  auto v = make_outer();
  std::string expected;
  jsondm::serialize(v, expected);
  char fn[] = "/tmp/jsondm_test_XXXXXX";
  int fd = mkstemp(fn);
  ASSERT_TRUE(fd >= 0);
  jsondm::serialize(v, fd);
  ASSERT_EQUAL(close(fd), 0);
  FILE *f = fopen(fn, "rb");
  ASSERT_TRUE(f != nullptr);
  std::string content;
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    content.append(buf, n);
  fclose(f);
  unlink(fn);
  ASSERT_EQUAL(content, expected);
}

void test_reuse_no_alloc() {
  // happy-path allocation freedom: reused string + reused struct
  auto v = make_outer();
  std::string out;
  out.clear();
  jsondm::serialize(v, out);
  jsondm_outer_t v2;
  jsondm::deserialize(out, v2);
  // prime capacities
  out.clear();
  jsondm::serialize(v, out);
  jsondm::deserialize(out, v2);
  size_t before = out.capacity();
  for (int i = 0; i < 100; i++) {
    out.clear();
    jsondm::serialize(v, out);
    jsondm::deserialize(out, v2);
  }
  ASSERT_TRUE(out.capacity() == before);
}

void test_control_status() {
  // first end-to-end slice: the mdns status payloads
  rtpmididns::mdns_status_t s;
  s.status = "Available";
  s.announcements.push_back({"a", 5004});
  s.announcements.push_back({"b", 5005});
  s.remote_announcements.push_back({"r", "host.example", 5006});

  std::string out;
  jsondm::serialize(s, out);
  // wire shape preserved (legacy nlohmann shape): object with status,
  // announcements [{name,port}], remote_announcements [{name,hostname,port}]
  ASSERT_EQUAL(
      out,
      "{\"status\":\"Available\",\"announcements\":["
      "{\"name\":\"a\",\"port\":5004},{\"name\":\"b\",\"port\":5005}],"
      "\"remote_announcements\":[{\"name\":\"r\",\"hostname\":\"host.example\","
      "\"port\":5006}]}");

  rtpmididns::mdns_status_t back;
  jsondm::deserialize(out, back);
  ASSERT_EQUAL(back.status, "Available");
  ASSERT_EQUAL(back.announcements.size(), (size_t)2);
  ASSERT_EQUAL(back.announcements[1].name, "b");
  ASSERT_EQUAL(back.announcements[1].port, 5005);
  ASSERT_EQUAL(back.remote_announcements.size(), (size_t)1);
  ASSERT_EQUAL(back.remote_announcements[0].hostname, "host.example");

  // fmt adapter for the struct
  std::string f = FMT::format("{}", s);
  ASSERT_EQUAL(f, out);
}

void test_peer_status_wire() {
  // byte-compat: RTP peer entry matches the legacy nlohmann shape exactly
  rtpmididns::rtp_peer_status_t s;
  s.name = "peer1";
  s.peer.latency_ms = {1.0, 2.0, 0.5};
  s.peer.status = "3";
  s.peer.local = {1, 2, "local", 0x11223344u, 5004, "host.local"};
  s.peer.remote = {"remote", 3, 0x55667788u, 5005, "host.remote"};
  s.id = 7;
  s.send_to = {1, 2};
  s.stats = {10, 20};
  s.type = "network_rtpmidi_peer_t";
  std::string out;
  jsondm::serialize(s, out);
  ASSERT_EQUAL(
      out,
      "{\"name\":\"peer1\",\"peer\":{\"latency_ms\":{\"last\":1,\"average\":2,"
      "\"stddev\":0.5},\"status\":\"3\",\"local\":{\"sequence_number\":1,"
      "\"sequence_number_ack\":2,\"name\":\"local\",\"ssrc\":287454020,"
      "\"port\":5004,\"hostname\":\"host.local\"},\"remote\":{\"name\":"
      "\"remote\","
      "\"sequence_number\":3,\"ssrc\":1432778632,\"port\":5005,"
      "\"hostname\":\"host.remote\"}},\"id\":7,\"send_to\":[1,2],"
      "\"stats\":{\"recv\":10,\"sent\":20},\"type\":\"network_rtpmidi_peer_"
      "t\"}");

  // error alternative
  rtpmididns::peer_error_t e{"boom"};
  out.clear();
  jsondm::serialize(e, out);
  ASSERT_EQUAL(out, "{\"error\":\"boom\"}");

  // variant serialization visits the active alternative
  rtpmididns::peer_status_variant_t v = s;
  out.clear();
  jsondm::serialize(v, out);
  ASSERT_EQUAL(out.find("\"id\":7") != std::string::npos, true);
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_roundtrip),        TEST(test_scalars),
      TEST(test_strings),          TEST(test_containers),
      TEST(test_optional_variant), TEST(test_array_mode),
      TEST(test_nested_and_depth), TEST(test_errors),
      TEST(test_fmt_adapter),      TEST(test_fd_sink),
      TEST(test_reuse_no_alloc),   TEST(test_control_status),
      TEST(test_peer_status_wire),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}

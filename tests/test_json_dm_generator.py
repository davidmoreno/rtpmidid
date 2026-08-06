#!/usr/bin/env python3
"""Tests for scripts/json_dm_to_cpp.py.

Usage: test_json_dm_generator.py <repo_root>

Runs the generator on fixture headers, checks fail-loud cases, byte-identical
regeneration, and compiles + runs the generated code end to end.
"""

import os
import subprocess
import sys
import tempfile

REPO = sys.argv[1] if len(sys.argv) > 1 else "."
GENERATOR = os.path.join(REPO, "scripts", "json_dm_to_cpp.py")
INCLUDE = os.path.join(REPO, "include")

FAILURES = []


def check(cond, msg):
    if not cond:
        FAILURES.append(msg)
        print(f"FAIL: {msg}")
    else:
        print(f"ok: {msg}")


def write(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)


def run_gen(tmp, *extra):
    out = os.path.join(tmp, "out")
    os.makedirs(out, exist_ok=True)
    cmd = [
        sys.executable,
        GENERATOR,
        "--header",
        out,
        "--source",
        out,
        *extra,
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r, out


def main():
    tmp = tempfile.mkdtemp(prefix="jsondm_gen_")

#-- - fixture 1 : happy path, all types -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    fixture = os.path.join(tmp, "src", "data.hpp")
    write(
        fixture,
        """#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace model {

/// [JSON-DM]
struct inner_t {
  std::string name;
  int port = 5004;
};

/// [JSON-DM]
struct outer_t {
  int id;
  unsigned count;
  uint64_t big;
  double ratio;
  float gain;
  bool ok;
  std::string name = "default";
  std::vector<int> items{1, 2, 3};
  std::vector<inner_t> peers;
  std::unordered_map<std::string, int> sizes;
  std::optional<std::string> nick;
  std::variant<std::monostate, int, std::string> choice;
  inner_t inner;
};

/// [JSON-DM-ARRAY]
struct point_t {
  double x;
  double y;
};

} // namespace model
""",
    )
    r, out = run_gen(tmp, "--input", fixture)
    check(r.returncode == 0, "happy path generates")
    check(
        os.path.exists(os.path.join(out, "data_jsondm.hpp"))
        and os.path.exists(os.path.join(out, "data_jsondm.cpp")),
        "output files exist",
    )
    hpp = open(os.path.join(out, "data_jsondm.hpp")).read()
    cpp = open(os.path.join(out, "data_jsondm.cpp")).read()
    check("serializer<model::inner_t>" in hpp, "qualified names in hpp")
    check("serializer<model::outer_t>" in hpp, "outer serializer declared")
    check("serializer<model::point_t>" in hpp, "array-mode struct declared")
    check("if (v.nick)" in cpp, "optional omission emitted")
    check("w.arr_begin();" in cpp, "array mode serializer emitted")
    check("if (i != 3)" in cpp or "if (i != 2)" in cpp, "array length check emitted")
    check("formatter<model::point_t>" in hpp, "formatter adapter emitted")

#-- - fixture 2 : fail - loud cases -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    bad_type = os.path.join(tmp, "src", "bad_type.hpp")
    write(
        bad_type,
        """#pragma once
#include <memory>
/// [JSON-DM]
struct bad_t {
  std::unique_ptr<int> ptr;
};
""",
    )
    r, _ = run_gen(tmp, "--input", bad_type)
    check(r.returncode != 0, "unsupported type fails loudly")
    check("bad_t" in r.stderr, "error names the struct")

    unmarked = os.path.join(tmp, "src", "unmarked.hpp")
    write(
        unmarked,
        """#pragma once
struct helper_t { int x; };
/// [JSON-DM]
struct uses_t {
  helper_t h;
};
""",
    )
    r, _ = run_gen(tmp, "--input", unmarked)
    check(r.returncode != 0, "unmarked nested struct fails loudly")
    check("helper_t" in r.stderr, "error names the missing type")

    bad_marker = os.path.join(tmp, "src", "bad_marker.hpp")
    write(
        bad_marker,
        """/// [JSON-DM-BOGUS]
struct x_t { int a; };
""",
    )
    r, _ = run_gen(tmp, "--input", bad_marker)
    check(r.returncode != 0, "unknown marker fails loudly")

    dup = os.path.join(tmp, "src", "dup1.hpp")
    dup2 = os.path.join(tmp, "src", "dup2.hpp")
    write(dup, "/// [JSON-DM]\nstruct same_t { int a; };\n")
    write(dup2, "/// [JSON-DM]\nstruct same_t { int b; };\n")
    r, _ = run_gen(tmp, "--input", dup, "--input", dup2)
    check(r.returncode != 0, "duplicate struct names fail loudly")

    pointer = os.path.join(tmp, "src", "pointer.hpp")
    write(pointer, "/// [JSON-DM]\nstruct p_t { int *p; };\n")
    r, _ = run_gen(tmp, "--input", pointer)
    check(r.returncode != 0, "pointer member fails loudly")

    const_member = os.path.join(tmp, "src", "const_member.hpp")
    write(const_member, "/// [JSON-DM]\nstruct c_t { const int x; };\n")
    r, _ = run_gen(tmp, "--input", const_member)
    check(r.returncode != 0, "const member fails loudly")

#-- - fixture 3 : INI mode(/// [INI-DM]) ------------------------------
    ini_fixture = os.path.join(tmp, "src", "conf.hpp")
    write(
        ini_fixture,
        """#pragma once
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace jsondm {
namespace ini {
template <> inline std::regex to_value<std::regex>(std::string_view v) {
  return std::regex(std::string(v));
}
template <> inline std::string to_text<std::regex>(const std::regex &) { return ""; }
} // namespace ini
} // namespace jsondm

/// [INI-DM]
struct conf_t {
  std::string name = "default";
  bool enabled = true;
  std::regex filter = std::regex(".*");
  /// [INI-DM]
  struct section_t {
    std::string host;
    int port;
  };
  /// [INI-DM]
  struct item_t {
    std::string a;
    std::string b;
  };
  section_t section;
  std::vector<item_t> items;
};
""",
    )
    r, out = run_gen(tmp, "--input", ini_fixture)
    check(r.returncode == 0, "ini struct generates")
    ini_hpp = open(os.path.join(out, "conf_jsondm.hpp")).read()
    ini_cpp = open(os.path.join(out, "conf_jsondm.cpp")).read()
    check("ini_deserializer<conf_t>" in ini_hpp, "ini deserializer declared")
    check("ini_serializer<conf_t>" in ini_hpp, "ini serializer declared")
    check("jsondm::serializer<conf_t>" not in ini_hpp, "no JSON code for INI structs")
    check('section == "general"' in ini_cpp, "general section handling")
    check('section == "items"' in ini_cpp, "repeated section handling")
    check("to_value<std::regex>" in ini_cpp, "regex converter emitted")
    check('throw jsondm::exception("{}:{}: Invalid key' in ini_cpp,
          "file:line error emitted")
#INI end - to - end : compile + round - trip
    ini_main = os.path.join(tmp, "ini_main.cpp")
    write(
        ini_main,
        r"""#include "conf_jsondm.hpp"
#include <cassert>
#include <cstdio>
#include <regex>
#include <rtpmidid/jsondm.hpp>
#include <string>

int main() {
    std::string cfg =
        "[general]\nname=hello\nenabled=false\nfilter=server:port/\\d+\n"
        "[section]\nhost=h\nport=5004\n"
        "[items]\na=1\nb=2\n"
        "[items]\na=3\nb=4\n";
    std::string text = cfg;
    jsondm::IniReader r(text, "conf.ini");
    conf_t c;
    jsondm::ini_deserializer<conf_t>::read(r, c);
    assert(c.name == "hello");
    assert(c.enabled == false);
    assert(std::regex_search("server:port/123", c.filter));
    assert(c.section.host == "h");
    assert(c.section.port == 5004);
    assert(c.items.size() == 2);
    assert(c.items[1].a == "3");
    // error on unknown key with file:line
    bool threw = false;
    try {
        std::string bad = "[general]\nnope=1\n";
        jsondm::IniReader rb(bad, "conf.ini");
        conf_t cb;
        jsondm::ini_deserializer<conf_t>::read(rb, cb);
    } catch (const jsondm::exception &e) {
        threw = true;
        assert(std::string(e.what()).find("conf.ini") != std::string::npos);
    }
    assert(threw);
    std::printf("INI OK\n");
    return 0;
}
""",
    )
    compile_cmd = [
        "g++", "-std=c++20", "-Wall", "-Werror",
        f"-I{INCLUDE}", f"-I{out}", f"-I{os.path.join(tmp, 'src')}",
        os.path.join(out, "conf_jsondm.cpp"), ini_main,
        "-o", os.path.join(tmp, "ini_test_bin"),
    ]
    c = subprocess.run(compile_cmd, capture_output=True, text=True)
    if c.returncode != 0:
        check(False, "ini generated code compiles: " + c.stderr[-500:])
    else:
        check(True, "ini generated code compiles")
        run = subprocess.run([os.path.join(tmp, "ini_test_bin")],
                             capture_output=True, text=True)
        check(run.returncode == 0 and "INI OK" in run.stdout,
              "ini generated code round-trips")

#-- - deterministic output -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    r1, out1 = run_gen(tmp, "--input", fixture)
    r2, out2 = run_gen(tmp, "--input", fixture)
    check(
        open(os.path.join(out1, "data_jsondm.cpp")).read()
        == open(os.path.join(out2, "data_jsondm.cpp")).read(),
        "regeneration is byte-identical",
    )

#-- - end - to - end compile + round - trip -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
    main_cpp = os.path.join(tmp, "main.cpp")
    write(
        main_cpp,
        """#include "data_jsondm.hpp"
#include <cassert>
#include <cstdio>
#include <rtpmidid/jsondm.hpp>

int main() {
    model::outer_t o;
    o.id = 1;
    o.count = 2;
    o.big = 9999999999ull;
    o.ratio = 0.25;
    o.gain = 1.5f;
    o.ok = true;
    o.name = "x";
    o.items = {4, 5};
    o.peers.push_back({"p", 5004});
    o.sizes["k"] = 7;
    o.nick = "n";
    o.choice = std::string("s");
    o.inner = {"i", 1};

    std::string out;
    jsondm::serialize(o, out);
    std::printf("roundtrip: %s\\n", out.c_str());

    model::outer_t o2;
    jsondm::deserialize(out, o2);
    assert(o2.id == 1);
    assert(o2.name == "x");
    assert(o2.peers.size() == 1);
    assert(o2.peers[0].name == "p");
    assert(o2.sizes.at("k") == 7);
    assert(std::get<std::string>(o2.choice) == "s");

    // array mode
    model::point_t p{1.5, 2.5};
    std::string out2;
    jsondm::serialize(p, out2);
    assert(out2 == "[1.5,2.5]");
    model::point_t p2;
    jsondm::deserialize(out2, p2);
    assert(p2.x == 1.5 && p2.y == 2.5);

    // errors carry the path
    bool threw = false;
    try {
        model::outer_t x;
        jsondm::deserialize(R"({"id": 1, "peers": [{"name": "a", "port": "bad"}]})", x);
    } catch (const jsondm::exception &e) {
        threw = true;
        assert(std::string(e.what()).find("peers[0].port") != std::string::npos);
    }
    assert(threw);
    std::printf("ALL OK\\n");
    return 0;
}
""",
    )
    compile_cmd = [
        "g++",
        "-std=c++20",
        "-Wall",
        "-Werror",
        f"-I{INCLUDE}",
        f"-I{out}",
        f"-I{os.path.join(tmp, 'src')}",
        os.path.join(out, "data_jsondm.cpp"),
        main_cpp,
        "-o",
        os.path.join(tmp, "test_bin"),
    ]
    c = subprocess.run(compile_cmd, capture_output=True, text=True)
    if c.returncode != 0:
        check(False, "generated code compiles: " + c.stderr[-500:])
    else:
        check(True, "generated code compiles")
        run = subprocess.run(
            [os.path.join(tmp, "test_bin")], capture_output=True, text=True
        )
        check(run.returncode == 0 and "ALL OK" in run.stdout, "generated code round-trips")
        if run.returncode != 0:
            print(run.stdout, run.stderr)

    print()
    if FAILURES:
        print(f"{len(FAILURES)} failures")
        sys.exit(1)
    print("ALL GENERATOR TESTS OK")


if __name__ == "__main__":
    main()

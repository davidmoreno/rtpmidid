#!/usr/bin/env python3
"""Parse // [dm-json] structs and // [dm-json:enum] enum class; emit dmjson serializers."""
from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple


@dataclass
class FieldOpt:
    json_key: Optional[str] = None
    omit_if_null: bool = False
    omit_if_empty: bool = False
    opaque: bool = False


@dataclass
class Field:
    ctype: str
    name: str
    opt: FieldOpt = field(default_factory=FieldOpt)


@dataclass
class StructDecl:
    name: str
    fields: List[Field]


@dataclass
class EnumDecl:
    name: str
    enumerators: List[str]


def parse_field_opts(comment: str) -> FieldOpt:
    fo = FieldOpt()
    m = re.search(r"//\s*\[dm-json:\s*([^\]]*)\]", comment)
    if not m:
        return fo
    body = m.group(1)
    km = re.search(r'key\s*=\s*"([^"]*)"', body)
    if km:
        fo.json_key = km.group(1)
    if re.search(r"\bomit_if_null\b", body):
        fo.omit_if_null = True
    if re.search(r"\bomit_if_empty\b", body):
        fo.omit_if_empty = True
    if re.search(r"\bopaque\b", body):
        fo.opaque = True
    return fo


def split_type_name(line: str) -> Optional[Tuple[str, str]]:
    line = line.strip()
    if "//" in line:
        line = line.split("//", 1)[0].strip()
    if not line.endswith(";"):
        return None
    line = line[:-1].strip()
    if not line or line.startswith("public:") or line.startswith("private:") or line.startswith("protected:"):
        return None
    m = re.match(r"^(.+)\s+([A-Za-z_]\w*)\s*$", line)
    if not m:
        return None
    typ, nam = m.group(1).strip(), m.group(2).strip()
    if "=" in nam:
        return None
    if not typ or not nam:
        return None
    return typ, nam


def extract_brace_body(src: str, open_brace: int) -> Optional[Tuple[str, int]]:
    depth = 0
    i = open_brace
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"':
            i += 1
            while i < n:
                if src[i] == "\\":
                    i += 2
                    continue
                if src[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return src[open_brace + 1 : i], i + 1
        i += 1
    return None


def parse_struct_fields(body: str) -> List[Field]:
    fields: List[Field] = []
    depth = 0
    cur: List[str] = []
    i = 0
    n = len(body)
    while i < n:
        ch = body[i]
        if depth == 0 and ch == "/" and i + 1 < n and body[i + 1] == "/":
            while i < n and body[i] != "\n":
                i += 1
            continue
        if depth == 0 and ch == "/" and i + 1 < n and body[i + 1] == "*":
            i += 2
            while i + 1 < n and not (body[i] == "*" and body[i + 1] == "/"):
                i += 1
            i = min(n, i + 2)
            continue
        if ch == "<":
            depth += 1
            cur.append(ch)
        elif ch == ">":
            depth = max(0, depth - 1)
            cur.append(ch)
        elif ch == ";" and depth == 0:
            raw = "".join(cur).strip()
            cur = []
            if not raw:
                i += 1
                continue
            cmt = ""
            if "//" in raw:
                a, _, b = raw.partition("//")
                raw, cmt = a.strip(), "//" + b
            stn = split_type_name(raw + ";")
            if not stn:
                i += 1
                continue
            typ, nam = stn
            fo = parse_field_opts(cmt)
            j = i + 1
            while j < n and body[j] in " \t\r":
                j += 1
            if j + 1 < n and body[j] == "/" and body[j + 1] == "/":
                c0 = j
                while j < n and body[j] != "\n":
                    j += 1
                mx = parse_field_opts(body[c0:j])
                if mx.json_key:
                    fo.json_key = mx.json_key
                if mx.omit_if_null:
                    fo.omit_if_null = True
                if mx.omit_if_empty:
                    fo.omit_if_empty = True
                if mx.opaque:
                    fo.opaque = True
            if j < n and body[j] == "\n":
                j += 1
            fields.append(Field(ctype=typ, name=nam, opt=fo))
            i = j
            continue
        else:
            cur.append(ch)
        i += 1
    return fields


def parse_enum_enumerators(body: str) -> List[str]:
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    out: List[str] = []
    for part in body.split(","):
        p = part.strip()
        if not p:
            continue
        if "=" in p:
            p = p.split("=", 1)[0].strip()
        tok = p.split()
        if tok and tok[0] not in ("class", "struct"):
            out.append(tok[0])
    return out


def parse_file(text: str) -> Tuple[List[StructDecl], List[EnumDecl]]:
    structs: List[StructDecl] = []
    enums: List[EnumDecl] = []
    pos = 0
    n = len(text)
    while pos < n:
        m = re.search(r"//\s*\[dm-json\]\s*\n\s*struct\s+(\w+)\s*\{", text[pos:])
        if not m:
            break
        abs_m = pos + m.start()
        name = m.group(1)
        open_b = text.find("{", abs_m)
        ex = extract_brace_body(text, open_b)
        if not ex:
            break
        body, after = ex
        structs.append(StructDecl(name, parse_struct_fields(body)))
        pos = after

    pos = 0
    while pos < n:
        m = re.search(
            r"//\s*\[dm-json:enum\]\s*\n\s*enum\s+class\s+(\w+)\s*(?::\s*\w+)?\s*\{", text[pos:]
        )
        if not m:
            break
        abs_m = pos + m.start()
        name = m.group(1)
        open_b = text.find("{", abs_m)
        ex = extract_brace_body(text, open_b)
        if not ex:
            break
        body, after = ex
        enums.append(EnumDecl(name, parse_enum_enumerators(body)))
        pos = after
    return structs, enums


SCALARS = {
    "bool",
    "int",
    "int32_t",
    "int64_t",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
    "size_t",
    "float",
    "double",
}


def strip_type(t: str) -> str:
    t = t.strip()
    if t.startswith("const "):
        t = t[6:].strip()
    return t


def tmpl_name(t: str) -> Optional[Tuple[str, str]]:
    """Split `foo<a,b>` into ('foo', 'a,b') using bracket depth."""
    t = strip_type(t)
    if "<" not in t or not t.endswith(">"):
        return None
    depth = 0
    start_lt = -1
    for i, c in enumerate(t):
        if c == "<" and depth == 0:
            start_lt = i
            break
    if start_lt < 0:
        return None
    base = t[:start_lt].strip()
    depth = 0
    for i in range(start_lt, len(t)):
        if t[i] == "<":
            depth += 1
        elif t[i] == ">":
            depth -= 1
            if depth == 0:
                return base, t[start_lt + 1 : i].strip()
    return None


def json_key(f: Field) -> str:
    return f.opt.json_key if f.opt.json_key else f.name


def topo_sort(structs: List[StructDecl]) -> List[StructDecl]:
    names = {s.name for s in structs}

    def refs(t: str) -> Set[str]:
        t = strip_type(t)
        if t in SCALARS or t == "std::string":
            return set()
        if t in names:
            return {t}
        tm = tmpl_name(t)
        if not tm:
            return set()
        base, inner = tm
        if base == "std::optional":
            return refs(inner)
        if base == "std::vector":
            return refs(inner)
        if base in ("std::map", "std::unordered_map"):
            parts = inner.split(",", 1)
            if len(parts) == 2:
                return refs(parts[1].strip())
        return set()

    deps: Dict[str, Set[str]] = {s.name: set() for s in structs}
    for s in structs:
        for f in s.fields:
            deps[s.name] |= refs(f.ctype)
    order: List[str] = []
    seen: Set[str] = set()

    def visit(x: str):
        if x in seen:
            return
        for d in sorted(deps[x]):
            if d in names:
                visit(d)
        seen.add(x)
        order.append(x)

    for nm in sorted(names):
        visit(nm)
    rank = {nm: i for i, nm in enumerate(order)}
    return sorted(structs, key=lambda s: rank[s.name])


def write_scalar_r(t: str, dst: str) -> str:
    b = strip_type(t)
    if b == "bool":
        return f"{dst} = r.read_bool();"
    if b in ("int", "int32_t"):
        return f"{dst} = static_cast<int32_t>(r.read_int64());"
    if b == "int64_t":
        return f"{dst} = r.read_int64();"
    if b == "uint8_t":
        return f"{dst} = static_cast<uint8_t>(r.read_uint64());"
    if b == "uint16_t":
        return f"{dst} = static_cast<uint16_t>(r.read_uint64());"
    if b == "uint32_t":
        return f"{dst} = static_cast<uint32_t>(r.read_uint64());"
    if b in ("uint64_t", "size_t"):
        return f"{dst} = static_cast<{b}>(r.read_uint64());"
    if b == "float":
        return f"{dst} = static_cast<float>(r.read_double());"
    if b == "double":
        return f"{dst} = r.read_double();"
    if b == "std::string":
        return f"r.read_string_into({dst});"
    raise ValueError(b)


def write_scalar_w(t: str, expr: str) -> str:
    b = strip_type(t)
    if b == "bool":
        return f"w.bool_value(static_cast<bool>({expr}));"
    if b in ("int", "int32_t", "int64_t"):
        return f"w.int_value(static_cast<int64_t>({expr}));"
    if b in ("uint8_t", "uint16_t", "uint32_t", "uint64_t", "size_t"):
        return f"w.uint_value(static_cast<uint64_t>({expr}));"
    if b == "float":
        return f"w.double_value(static_cast<double>({expr}));"
    if b == "double":
        return f"w.double_value({expr});"
    if b == "std::string":
        return f"w.string_value({expr});"
    raise ValueError(b)


def emit_read_value(
    t: str, dst: str, ind: str, structs: Set[str], enums: Set[str], depth: int = 0
) -> List[str]:
    t = strip_type(t)
    if t in SCALARS or t == "std::string":
        return [ind + write_scalar_r(t, dst)]
    if t in enums:
        return [ind + f"if (!from_json_{t}(r, {dst})) return false;"]
    if t in structs:
        return [ind + f"if (!from_json(r, {dst})) return false;"]  # reader overload
    tm = tmpl_name(t)
    if not tm:
        raise ValueError(t)
    base, inner = tm
    if base == "std::vector":
        inner_t = strip_type(inner)
        el = f"_el{depth}"
        return [
            ind + f"{dst}.clear();",
            ind + "r.expect_array_begin();",
            ind + "while (!r.try_consume_array_end()) {",
            ind + f"  {inner_t} {el}{{}};",
        ] + emit_read_value(inner, el, ind + "  ", structs, enums, depth + 1) + [
            ind + f"  {dst}.push_back(std::move({el}));",
            ind + "  if (r.try_consume_array_end())",
            ind + "    break;",
            ind + "  r.expect_comma();",
            ind + "}",
        ]
    if base in ("std::map", "std::unordered_map"):
        parts = inner.split(",", 1)
        vt = parts[1].strip()
        mk = f"_mk{depth}"
        mv = f"_mv{depth}"
        return [
            ind + f"{dst}.clear();",
            ind + "r.expect_object_begin();",
            ind + "while (!r.try_consume_object_end()) {",
            ind + f"  std::string {mk};",
            ind + f"  r.read_string_key_into({mk});",
            ind + "  r.expect_colon();",
            ind + f"  {vt} {mv}{{}};",
        ] + emit_read_value(vt, mv, ind + "  ", structs, enums, depth + 1) + [
            ind + f"  {dst}[std::move({mk})] = std::move({mv});",
            ind + "  if (r.try_consume_object_end())",
            ind + "    break;",
            ind + "  r.expect_comma();",
            ind + "}",
        ]
    raise ValueError(t)


def emit_write_value(t: str, expr: str, ind: str, structs: Set[str], enums: Set[str]) -> List[str]:
    t = strip_type(t)
    if t in SCALARS or t == "std::string":
        return [ind + write_scalar_w(t, expr)]
    if t in enums:
        return [ind + f"to_json({expr}, w);"]
    if t in structs:
        return [ind + f"to_json({expr}, w);"]
    tm = tmpl_name(t)
    if not tm:
        raise ValueError(t)
    base, inner = tm
    if base == "std::vector":
        return [
            ind + "w.begin_array();",
            ind + f"for (const auto &el : {expr}) {{",
            ind + "  w.array_item();",
        ] + emit_write_value(inner, "el", ind + "  ", structs, enums) + [ind + "}", ind + "w.end_array();"]
    if base in ("std::map", "std::unordered_map"):
        parts = inner.split(",", 1)
        vt = parts[1].strip()
        return [
            ind + "w.begin_object();",
            ind + f"for (const auto &kv : {expr}) {{",
            ind + "  w.key(kv.first);",
        ] + emit_write_value(vt, "kv.second", ind + "  ", structs, enums) + [ind + "}", ind + "w.end_object();"]
    raise ValueError(t)


def emit_to_json_struct(s: StructDecl, structs: Set[str], enums: Set[str]) -> List[str]:
    lines = [f"void to_json(const ::rtpmididns::{s.name} &o, writer_t &w) {{", "  w.begin_object();"]
    for f in s.fields:
        k = json_key(f)
        t = strip_type(f.ctype)
        acc = f"o.{f.name}"
        tm = tmpl_name(t)
        if tm and tm[0] == "std::optional":
            inner = tm[1]
            lines.append(f"  if ({acc}) {{")
            lines.append(f'    w.key("{k}");')
            lines.extend("    " + x for x in emit_write_value(inner, f"(*{acc})", "", structs, enums))
            lines.append("  }")
            continue
        if tm and tm[0] == "std::vector" and f.opt.omit_if_empty:
            lines.append(f"  if (!{acc}.empty()) {{")
            lines.append(f'    w.key("{k}");')
            lines.extend("    " + x for x in emit_write_value(t, acc, "", structs, enums))
            lines.append("  }")
            continue
        if strip_type(t) == "std::string" and f.opt.omit_if_empty:
            lines.append(f"  if (!{acc}.empty()) {{")
            lines.append(f'    w.key("{k}");')
            lines.append("    " + write_scalar_w(t, acc))
            lines.append("  }")
            continue
        if f.opt.opaque and strip_type(t) == "std::string":
            lines.append(f'  w.key("{k}");')
            lines.append(f"  w.raw({acc});")
            continue
        lines.append(f'  w.key("{k}");')
        lines.extend("  " + x for x in emit_write_value(t, acc, "", structs, enums))
    lines.append("  w.end_object();")
    lines.append("}")
    return lines


def emit_from_json_struct(s: StructDecl, structs: Set[str], enums: Set[str]) -> List[str]:
    lines = [
        f"bool from_json(reader_t &r, ::rtpmididns::{s.name} &o) noexcept {{",
        "  try {",
    ]
    for f in s.fields:
        lines.append(f"    bool seen_{f.name} = false;")
    lines += [
        "    while (true) {",
        "      if (r.try_consume_object_end())",
        "        break;",
        "      std::string key;",
        "      r.read_string_key_into(key);",
        "      r.expect_colon();",
        "      bool hit = false;",
    ]
    for i, f in enumerate(s.fields):
        k = json_key(f)
        t = strip_type(f.ctype)
        acc = f"o.{f.name}"
        tm = tmpl_name(t)
        prefix = "      if (" if i == 0 else "      else if ("
        lines.append(f'{prefix}key == "{k}") {{')
        lines.append(f"        if (seen_{f.name}) return false;")
        lines.append(f"        seen_{f.name} = true;")
        if f.opt.opaque and strip_type(t) == "std::string":
            lines.append(f"        r.read_raw_into({acc});")
        elif tm and tm[0] == "std::optional":
            inner = tm[1]
            lines.append("        r.skip_ws();")
            lines.append("        if (r.peek() == 'n') {")
            lines.append("          r.read_null();")
            lines.append(f"          {acc}.reset();")
            lines.append("        } else {")
            lines.append(f"          {acc}.emplace();")
            lines.extend(
                "          "
                + x
                for x in emit_read_value(inner, f"(*{acc})", "", structs, enums, 0)
            )
            lines.append("        }")
        else:
            lines.extend("        " + x for x in emit_read_value(t, acc, "", structs, enums, 0))
        lines.append("        hit = true;")
        lines.append("      }")
    lines += [
        "      if (!hit)",
        "        r.skip_value();",
        "      if (r.try_consume_object_end())",
        "        break;",
        "      r.expect_comma();",
        "    }",
        "    return true;",
        "  } catch (...) {",
        "    return false;",
        "  }",
        "}",
        f"bool from_json(std::string_view sv, ::rtpmididns::{s.name} &o) noexcept {{",
        "  try {",
        "    reader_t r(sv);",
        "    r.expect_object_begin();",
        "    return from_json(r, o);",
        "  } catch (...) {",
        "    return false;",
        "  }",
        "}",
    ]
    return lines


def emit_enum(e: EnumDecl) -> Tuple[List[str], List[str]]:
    h: List[str] = [
        f"void to_json(::rtpmididns::{e.name} v, writer_t &w);",
        f"bool from_json_{e.name}(reader_t &r, ::rtpmididns::{e.name} &out);",
    ]
    cpp: List[str] = [
        f"void to_json(::rtpmididns::{e.name} v, writer_t &w) {{",
        "  switch (v) {",
    ]
    for ev in e.enumerators:
        cpp.append(f"  case ::rtpmididns::{e.name}::{ev}:")
        cpp.append(f'    w.string_value("{ev}"); break;')
    cpp += ["  default: w.string_value(\"?\"); break;", "  }", "}"]
    cpp += [
        f"bool from_json_{e.name}(reader_t &r, ::rtpmididns::{e.name} &out) {{",
        "  std::string s;",
        "  r.read_string_into(s);",
    ]
    for ev in e.enumerators:
        cpp.append(f'  if (s == "{ev}") {{ out = ::rtpmididns::{e.name}::{ev}; return true; }}')
    cpp += ["  return false;", "}"]
    return h, cpp


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--header", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("inputs", nargs="+")
    args = ap.parse_args()

    all_structs: List[StructDecl] = []
    all_enums: List[EnumDecl] = []
    for path in args.inputs:
        with open(path, "r", encoding="utf-8") as f:
            txt = f.read()
        st, en = parse_file(txt)
        all_structs.extend(st)
        all_enums.extend(en)

    if not all_structs and not all_enums:
        print("dm_json_gen: no [dm-json] types in inputs", file=sys.stderr)
        return 1

    all_structs = topo_sort(all_structs)
    struct_set = {s.name for s in all_structs}
    enum_set = {e.name for e in all_enums}

    os.makedirs(args.out_dir, exist_ok=True)
    out_h = os.path.join(args.out_dir, args.header)
    out_cpp = os.path.join(args.out_dir, args.source)

    hdr: List[str] = [
        "// Generated by scripts/dm_json_gen.py — do not edit.",
        "#pragma once",
        "#include <string>",
        "#include <string_view>",
        "#include <utility>",
        "#include <rtpmidid/dm_json/runtime.hpp>",
    ]
    for p in args.inputs:
        hdr.append(f'#include "{os.path.basename(p)}"')

    hdr += ["", "namespace rtpmididns::dmjson {", ""]

    cpp: List[str] = [
        f'#include "{args.header}"',
        "",
        "namespace rtpmididns::dmjson {",
        "",
    ]

    for e in all_enums:
        eh, ec = emit_enum(e)
        hdr.extend(eh)
        hdr.append("")
        cpp.extend(ec)
        cpp.append("")

    for s in all_structs:
        hdr.append(f"void to_json(const ::rtpmididns::{s.name} &o, writer_t &w);")
        hdr.append(f"bool from_json(reader_t &r, ::rtpmididns::{s.name} &o) noexcept;")
        hdr.append(f"bool from_json(std::string_view sv, ::rtpmididns::{s.name} &o) noexcept;")
        hdr.append(f"inline std::string to_json(const ::rtpmididns::{s.name} &o) {{")
        hdr.append("  writer_t w;")
        hdr.append("  to_json(o, w);")
        hdr.append("  std::string out;")
        hdr.append("  w.swap_into_string(out);")
        hdr.append("  return out;")
        hdr.append("}")
        hdr.append("")

    hdr.append("} // namespace rtpmididns::dmjson")
    hdr.append("")

    for s in all_structs:
        cpp.extend(emit_to_json_struct(s, struct_set, enum_set))
        cpp.append("")
        cpp.extend(emit_from_json_struct(s, struct_set, enum_set))
        cpp.append("")

    cpp.append("} // namespace rtpmididns::dmjson")
    cpp.append("")

    with open(out_h, "w", encoding="utf-8") as f:
        f.write("\n".join(hdr))
    with open(out_cpp, "w", encoding="utf-8") as f:
        f.write("\n".join(cpp))

    print("dm_json_gen:", out_h, out_cpp)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

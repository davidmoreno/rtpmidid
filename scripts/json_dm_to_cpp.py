#!/bin/python3
"""
json-dm code generator.

Scans C++ headers for structs decorated with a `/// [JSON-DM]` comment
(object mode) or `/// [JSON-DM-ARRAY]` (positional-array mode) and emits a
per-input pair of files:

    <name>_jsondm.hpp  - serializer/deserializer specialization declarations
                         and FMT::formatter adapters
    <name>_jsondm.cpp  - specialization definitions

The generated code relies on the jsondm runtime (include/rtpmidid/jsondm.hpp).

Usage:
    json_dm_to_cpp.py --input foo.hpp [--input bar.hpp ...] \\
                      --header <outdir> --source <outdir>

Fail-loud: any member type outside the supported set aborts generation with
the struct and member named.
"""

import argparse
import re
import sys
from dataclasses import dataclass, field

MARKER_OBJECT = "/// [JSON-DM]"
MARKER_ARRAY = "/// [JSON-DM-ARRAY]"

# ---------------------------------------------------------------------------
# Tokenizer helpers
# ---------------------------------------------------------------------------


class ParseError(Exception):
    pass


def skip_comment(text, i):
    """If text[i:] starts a comment, return the index after it."""
    n = len(text)
    if text[i : i + 2] == "//":
        j = text.find("\n", i)
        return n if j < 0 else j + 1
    if text[i : i + 2] == "/*":
        j = text.find("*/", i + 2)
        if j < 0:
            raise ParseError("unterminated /* comment")
        return j + 2
    return i


def skip_string(text, i):
    """Skip a C string/char literal starting at text[i] ('"')."""
    n = len(text)
    j = i + 1
    while j < n:
        c = text[j]
        if c == "\\":
            j += 2
            continue
        if c == '"':
            return j + 1
        if c == "\n":
            raise ParseError("unterminated string literal")
        j += 1
    raise ParseError("unterminated string literal")


def skip_char(text, i):
    n = len(text)
    j = i + 1
    while j < n:
        c = text[j]
        if c == "\\":
            j += 2
            continue
        if c == "'":
            return j + 1
        if c == "\n":
            raise ParseError("unterminated char literal")
        j += 1
    raise ParseError("unterminated char literal")


def split_top_level(text, sep):
    """Split text on sep at bracket depth 0 (respecting <>{}(), strings, comments)."""
    parts = []
    cur = []
    depth = 0
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        j = skip_comment(text, i)
        if j != i:
            cur.append(text[i:j])
            i = j
            continue
        if c == '"':
            j = skip_string(text, i)
            cur.append(text[i:j])
            i = j
            continue
        if c == "'":
            j = skip_char(text, i)
            cur.append(text[i:j])
            i = j
            continue
        if c in "<([{":
            depth += 1
        elif c in ">)]}":
            depth -= 1
        elif c == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    parts.append("".join(cur))
    return parts


def find_struct_after(text, marker_index):
    """Find `struct Name` after marker_index (skipping comments/blank lines).

    Returns (full_name, body_start_index) where body_start_index points at
    the '{' opening the struct body. full_name includes any enclosing
    namespace context provided by the caller (not parsed here).
    """
    n = len(text)
    i = marker_index
    while i < n:
        j = skip_comment(text, i)
        if j != i:
            i = j
            continue
        line_end = text.find("\n", i)
        if line_end < 0:
            line_end = n
        line = text[i:line_end].strip()
        if line == "":
            i = line_end + 1
            continue
        m = re.match(r"struct\s+(\w+)", line)
        if m:
            name = m.group(1)
            rest = text[i + m.end() : line_end]
            brace = rest.find("{")
            # struct might open on a later line
            j = i + m.end()
            while brace < 0 and j < n:
                k = skip_comment(text, j)
                if k != j:
                    j = k
                    continue
                c = text[j]
                if c == "{":
                    brace = j - (i + m.end())
                    break
                if c == "\n":
                    pass
                elif c not in " \t\r\n":
                    raise ParseError(
                        f"expected '{{' after struct {name}, found {c!r}"
                    )
                j += 1
            if brace < 0:
                raise ParseError(f"struct {name} has no body")
            return name, i + m.end() + brace
        raise ParseError(f"expected struct declaration after marker, found: {line!r}")
    raise ParseError("marker not followed by a struct")


def parse_struct_body(body):
    """Split a struct body into top-level member declaration statements.

    Nested struct/class/enum/union definitions and their ';' are skipped
    entirely. Returns list of statement strings.
    """
    stmts = []
    cur = []
    depth = 0  # extra nesting beyond member level
    i = 0
    n = len(body)
    nested_kw = None
    while i < n:
        c = body[i]
        j = skip_comment(body, i)
        if j != i:
            cur.append(body[i:j])
            i = j
            continue
        if c == '"':
            j = skip_string(body, i)
            cur.append(body[i:j])
            i = j
            continue
        if c == "'":
            j = skip_char(body, i)
            cur.append(body[i:j])
            i = j
            continue
        if depth == 0:
            first_word = re.match(r"\s*(\w+)", "".join(cur))
            if c == ";" and first_word and first_word.group(1) in (
                "struct",
                "class",
                "enum",
                "union",
            ):
                # end of a nested type definition
                cur = []
                depth = 0
                i += 1
                continue
            if c == "{":
                depth = 1
                cur.append(c)
            elif c == "}":
                # closing the whole struct body (caller consumed it)
                cur.append(c)
                depth = 1  # treat as nested; caller stops before this anyway
            elif c == ";":
                stmts.append("".join(cur))
                cur = []
            else:
                cur.append(c)
        else:
            if c in "<([{":
                depth += 1
            elif c in ">)]}":
                depth -= 1
            cur.append(c)
        i += 1
    return stmts


# ---------------------------------------------------------------------------
# Member parsing
# ---------------------------------------------------------------------------

BUILTIN_RE = re.compile(
    r"^(std::)?(u?int(8|16|32|64)_t|size_t|ssize_t|bool|char|schar|uchar|short|"
    r"ushort|int|uint|long|ulong|llong|ullong|float|double)$"
)

NON_MEMBER_WORDS = {
    "struct",
    "class",
    "enum",
    "union",
    "using",
    "typedef",
    "template",
    "friend",
    "inline",
    "explicit",
}


@dataclass
class Member:
    name: str
    type_text: str  # normalized (no whitespace)
    category: str  # 'primitive' | 'string' | 'vector' | 'map' | 'optional' | 'variant' | 'struct'
    element: "Member | None" = None  # vector/optional element
    alternatives: list = field(default_factory=list)  # variant alternatives
    map_value: "Member | None" = None
    is_optional: bool = False


def normalize_type(t):
    t = re.sub(r"\s+", " ", t).strip()
    for a, b in (
        ("unsigned long long int", "ullong"),
        ("unsigned long int", "ulong"),
        ("long long int", "llong"),
        ("long int", "long"),
        ("unsigned long long", "ullong"),
        ("unsigned long", "ulong"),
        ("long long", "llong"),
        ("unsigned short int", "ushort"),
        ("short int", "short"),
        ("unsigned short", "ushort"),
        ("unsigned char", "uchar"),
        ("signed char", "schar"),
        ("unsigned int", "uint"),
        ("unsigned", "uint"),
        ("signed", "int"),
    ):
        t = t.replace(a, b)
    return re.sub(r"\s+", "", t)


def strip_comments(text):
    """Remove C/C++ comments from text (for regex scanning)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        j = skip_comment(text, i)
        if j != i:
            out.append(" " * (j - i))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def collect_aliases(text):
    """Collect `using X = <type>;` aliases (simple name -> body)."""
    clean = strip_comments(text)
    aliases = {}
    for m in re.finditer(r"\busing\s+(\w+)\s*=\s*([^;]+);", clean):
        aliases[m.group(1)] = m.group(2).strip()
    return aliases


def expand_aliases(t, aliases, depth=0):
    """Expand user type aliases inside a type text (recursively)."""
    if depth > 8:
        raise ParseError(f"alias expansion cycle involving: {t}")
    for name, body in aliases.items():
        pat = re.compile(r"(?<![\w:]){}(?![\w])".format(re.escape(name)))
        if pat.search(t):
            return expand_aliases(pat.sub(lambda m: body, t), aliases, depth + 1)
    return t


def classify_type(t, marked_set, context, aliases=None):
    """Classify a normalized type string.

    marked_set: dict simple_name -> full_name of JSON-DM structs.
    aliases: optional dict of `using X = ...` type aliases to expand.
    Returns a Member. Raises ParseError on unsupported types.
    """
    if aliases:
        t = expand_aliases(t, aliases)
    t = t.strip()
    if t == "":
        raise ParseError("empty member type")
    if "::" in t and t.startswith("std::") is False:
        # namespace-qualified: keep as-is; simple name is last segment
        pass
    if BUILTIN_RE.match(t):
        return Member(name="", type_text=t, category="primitive")
    if t in ("std::string", "string"):
        return Member(name="", type_text=t, category="string")
    if t == "std::monostate" or t == "monostate":
        return Member(name="", type_text=t, category="null")

    def _wrap(kind, inner, **kw):
        return Member(name="", type_text=t, category=kind, element=inner, **kw)

    for prefix, kind in (
        ("std::vector<", "vector"),
        ("vector<", "vector"),
        ("std::optional<", "optional"),
        ("optional<", "optional"),
    ):
        if t.startswith(prefix) and t.endswith(">"):
            inner = t[len(prefix) : -1]
            return _wrap(kind, classify_type(inner, marked_set, context, aliases))
    for prefix in ("std::variant<", "variant<"):
        if t.startswith(prefix) and t.endswith(">"):
            inner = t[len(prefix) : -1]
            alts = split_top_level(inner, ",")
            if not alts:
                raise ParseError("empty variant")
            alts = [classify_type(a, marked_set, context, aliases) for a in alts]
            return Member(
                name="", type_text=t, category="variant", alternatives=alts
            )
    for prefix in ("std::unordered_map<", "unordered_map<"):
        if t.startswith(prefix) and t.endswith(">"):
            inner = t[len(prefix) : -1]
            parts = split_top_level(inner, ",")
            if len(parts) != 2:
                raise ParseError(
                    f"unordered_map needs exactly 2 type args, got {len(parts)}"
                )
            key, value = (p.strip() for p in parts)
            if key not in ("std::string", "string"):
                raise ParseError(f"unordered_map key must be std::string, got {key}")
            return Member(
                name="",
                type_text=t,
                category="map",
                map_value=classify_type(value, marked_set, context, aliases),
            )
    # user type: must be a marked JSON-DM struct
    simple = t.split("::")[-1]
    if simple in marked_set:
        return Member(name="", type_text=t, category="struct")
    raise ParseError(
        f"member of {context} has unsupported type '{t}': it is neither a "
        f"supported builtin nor a /// [JSON-DM] marked struct"
    )


def parse_member_stmt(stmt, marked_set, context, aliases=None):
    """Parse one member declaration statement into a Member (name filled)."""
    s = stmt.strip()
    if s == "":
        return None
    first_word = re.match(r"(\w+)", s)
    if first_word and first_word.group(1) in NON_MEMBER_WORDS:
        return None  # nested type, using, etc. (struct bodies were skipped already)
    if s.startswith("//") or s.startswith("/*") or s.startswith("#"):
        return None

    # strip default initializer: first top-level '=' , '{' or '(' after the type
    cut = -1
    depth = 0
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == '"':
            i = skip_string(s, i)
            continue
        if c == "'":
            i = skip_char(s, i)
            continue
        if c in "<([{":
            depth += 1
        elif c in ">)]}":
            depth -= 1
        elif c in "= {(" and depth == 0 and c != " ":
            # '=' or '{' or '(' starting the initializer
            # ignore '{' only if we already have a type+name (brace init)
            cut = i
            break
        i += 1
    if cut >= 0:
        s = s[:cut].strip()

    # detect unsupported declarator forms (pointers, refs, arrays, functions)
    depth = 0
    has_asterisk = False
    has_amp = False
    has_bracket = False
    for i, c in enumerate(s):
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        elif depth == 0:
            if c == "*":
                has_asterisk = True
            elif c == "&":
                has_amp = True
            elif c == "[":
                has_bracket = True
            elif c == "(":
                raise ParseError(
                    f"member '{s}' in {context}: function/constructor declarator not supported"
                )
    if has_bracket:
        raise ParseError(f"member '{s}' in {context}: array declarator not supported")
    if has_asterisk or has_amp:
        raise ParseError(f"member '{s}' in {context}: pointer/reference not supported")

    # find the declarator name: the last identifier at angle-bracket depth 0
    idents = []
    depth = 0
    i = 0
    while i < len(s):
        c = s[i]
        if c.isalpha() or c == "_":
            j = i
            while j < len(s) and (s[j].isalnum() or s[j] == "_"):
                j += 1
            word = s[i:j]
            if depth == 0:
                idents.append((i, word))
            i = j
            continue
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        i += 1
    if not idents:
        raise ParseError(f"member '{s}' in {context}: no declarator name found")
    name_pos, name = idents[-1]
    type_text = s[:name_pos].strip()
    if type_text == "":
        raise ParseError(f"member '{s}' in {context}: missing type")

    m = classify_type(normalize_type(type_text), marked_set, context, aliases)
    m.name = name
    if m.category == "null":
        raise ParseError(
            f"member '{name}' in {context}: std::monostate is only valid inside a variant"
        )
    return m


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------


@dataclass
class StructInfo:
    simple_name: str
    full_name: str
    array_mode: bool
    source_file: str
    marker_end: int = 0
    members: list = field(default_factory=list)


def emit_serializer(si, indent="    "):
    """Emit the body of serializer<T>::write for one struct."""
    lines = []
    if si.array_mode:
        lines.append(f"{indent}w.arr_begin();")
        for idx, m in enumerate(si.members):
            if m.category == "optional":
                # array mode: no omission, null-or-value
                lines.append(
                    f"{indent}{{ jsondm::Writer::member_guard g(w, {idx}); "
                    f"jsondm::write(w, v.{m.name}); }}"
                )
            else:
                lines.append(
                    f"{indent}{{ jsondm::Writer::member_guard g(w, {idx}); "
                    f"jsondm::write(w, v.{m.name}); }}"
                )
        lines.append(f"{indent}w.arr_end();")
    else:
        lines.append(f"{indent}w.obj_begin();")
        for m in si.members:
            if m.category == "optional":
                lines.append(
                    f"{indent}if (v.{m.name}) {{ jsondm::Writer::member_guard "
                    f"g(w, \"{m.name}\"); jsondm::write(w, *v.{m.name}); }}"
                )
            else:
                lines.append(
                    f"{indent}{{ jsondm::Writer::member_guard g(w, \"{m.name}\"); "
                    f"jsondm::write(w, v.{m.name}); }}"
                )
        lines.append(f"{indent}w.obj_end();")
    return "\n".join(lines)


def emit_deserializer(si, indent="    "):
    lines = []
    if si.array_mode:
        lines.append(f"{indent}r.arr_begin();")
        lines.append(f"{indent}size_t i = 0;")
        lines.append(f"{indent}while (r.next_elem()) {{")
        lines.append(f"{indent}    jsondm::Reader::member_guard g(r, i);")
        for k, m in enumerate(si.members):
            op = "if" if k == 0 else "else if"
            lines.append(
                f"{indent}    {op} (i == {k}) jsondm::read(r, v.{m.name});"
            )
        lines.append(f"{indent}    else r.skip_value();")
        lines.append(f"{indent}    ++i;")
        lines.append(f"{indent}}}")
        lines.append(f"{indent}r.arr_end();")
        lines.append(
            f'{indent}if (i != {len(si.members)}) r.fail("{len(si.members)} array elements", '
            f'FMT::format("{{}} elements", i));'
        )
    else:
        lines.append(f"{indent}r.obj_begin();")
        lines.append(f"{indent}while (r.next_key()) {{")
        lines.append(f"{indent}    auto key = r.key();")
        for k, m in enumerate(si.members):
            op = "if" if k == 0 else "else if"
            lines.append(
                f'{indent}    {op} (key == "{m.name}") {{ '
                f'jsondm::Reader::member_guard g(r, "{m.name}"); '
                f"jsondm::read(r, v.{m.name}); }}"
            )
        lines.append(f"{indent}    else r.skip_value();")
        lines.append(f"{indent}}}")
        lines.append(f"{indent}r.obj_end();")
    return "\n".join(lines)


def generate_hpp(si, input_basename):
    decls = []
    for s in si:
        decls.append(
            f"template <> struct jsondm::serializer<{s.full_name}> {{\n"
            f"  static void write(const {s.full_name} &, jsondm::Writer &);\n"
            f"}};\n"
            f"template <> struct jsondm::deserializer<{s.full_name}> {{\n"
            f"  static void read(jsondm::Reader &, {s.full_name} &);\n"
            f"}};"
        )
    formatters = "\n".join(
        f"template <> struct FMT::formatter<{s.full_name}> : "
        f"jsondm::formatter_base<{s.full_name}> {{}};"
        for s in si
    )
    return f"""// Autogenerated by scripts/json_dm_to_cpp.py — do not edit.
#pragma once
#include <rtpmidid/jsondm.hpp>
#include \"{input_basename}\"

{chr(10).join(decls)}

{formatters}
"""


def generate_cpp(si, hpp_name, extra_includes):
    parts = []
    for s in si:
        parts.append(
            f"void jsondm::serializer<{s.full_name}>::write(\n"
            f"    const {s.full_name} &v, jsondm::Writer &w) {{\n"
            f"{emit_serializer(s)}\n"
            f"}}"
        )
        parts.append(
            f"void jsondm::deserializer<{s.full_name}>::read(\n"
            f"    jsondm::Reader &r, {s.full_name} &v) {{\n"
            f"{emit_deserializer(s)}\n"
            f"}}"
        )
    extra = "\n".join(f'#include "{e}"' for e in sorted(extra_includes))
    if extra:
        extra += "\n"
    return f"""// Autogenerated by scripts/json_dm_to_cpp.py — do not edit.
#include "{hpp_name}"
#include <rtpmidid/jsondm.hpp>
{extra}
namespace jsondm {{
{chr(10).join(parts)}
}} // namespace jsondm
"""


def collect_user_types(m, out):
    """Collect simple names of user struct types referenced by a member."""
    if m is None:
        return
    if m.category == "struct":
        out.add(m.type_text.split("::")[-1])
    if m.element:
        collect_user_types(m.element, out)
    if m.map_value:
        collect_user_types(m.map_value, out)
    for a in m.alternatives:
        collect_user_types(a, out)


# ---------------------------------------------------------------------------
# Main pipeline
# ---------------------------------------------------------------------------


def find_markers(text):
    """Yield (marker_kind, start, end) for every JSON-DM marker line.

    The marker must be the only content on its line (a doc comment
    mentioning the marker in prose must not trigger); a malformed
    JSON-DM-ish marker fails loudly."""
    for m in re.finditer(r"^[ \t]*///[ \t]*(\[[^\]]*\])[ \t]*$", text, re.M):
        token = m.group(1)
        if token == "[JSON-DM]":
            yield "object", m.start(1), m.end(1)
        elif token == "[JSON-DM-ARRAY]":
            yield "array", m.start(1), m.end(1)
        elif "JSON-DM" in token:
            raise ParseError(f"unknown JSON-DM marker: {token!r}")


def compute_struct_context(text, marker_starts):
    """For each marker start position, return the list of enclosing struct
    simple names (nesting context), computed by a tokenizing scan."""
    stack = []  # (kind, name) kind in {'struct','ns','other'}
    contexts = {}
    i = 0
    n = len(text)
    prev_ident = None
    prev_prev_ident = None
    marker_set = set(marker_starts)

    def record(mp):
        contexts[mp] = [
            name for kind, name in stack if kind in ("struct", "ns") and name
        ]

    while i < n:
        c = text[i]
        j = skip_comment(text, i)
        if j != i:
            # a comment spans [i, j); markers live inside it
            for mp in marker_starts:
                if i <= mp < j and mp not in contexts:
                    record(mp)
            i = j
            continue
        if c == '"':
            i = skip_string(text, i)
            continue
        if c == "'":
            i = skip_char(text, i)
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            prev_prev_ident = prev_ident
            prev_ident = text[i:j]
            i = j
            continue
        if c == "{":
            if prev_prev_ident in ("struct", "class") and prev_ident:
                stack.append(("struct", prev_ident))
            elif prev_prev_ident == "namespace":
                stack.append(("ns", prev_ident or ""))
            else:
                stack.append(("other", None))
            prev_ident = None
            prev_prev_ident = None
        elif c == "}":
            if stack:
                stack.pop()
        i += 1
    return contexts


def struct_body_extent(text, body_open):
    """Return (body_text, end_index_after_closing_brace)."""
    depth = 1
    i = body_open + 1
    n = len(text)
    while i < n and depth > 0:
        c = text[i]
        j = skip_comment(text, i)
        if j != i:
            i = j
            continue
        if c == '"':
            i = skip_string(text, i)
            continue
        if c == "'":
            i = skip_char(text, i)
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    if depth != 0:
        raise ParseError("unbalanced braces in struct body")
    return text[body_open + 1 : i - 1], i


def scan_inputs(inputs):
    """Parse all inputs; returns (structs, aliases)."""
    structs = []
    simple_to_full = {}
    all_aliases = {}
    for path in inputs:
        with open(path) as f:
            text = f.read()
        all_aliases.update(collect_aliases(text))
        markers = list(find_markers(text))
        contexts = compute_struct_context(
            text, [start for _, start, _ in markers]
        )
        for kind, start, end in markers:
            name, body_open = find_struct_after(text, end)
            prefix = contexts.get(start, [])
            full_name = "::".join(prefix + [name]) if prefix else name
            if name in simple_to_full:
                raise ParseError(
                    f"duplicate JSON-DM struct name '{name}' "
                    f"(already from {simple_to_full[name]})"
                )
            simple_to_full[name] = path
            structs.append(
                StructInfo(
                    simple_name=name,
                    full_name=full_name,
                    array_mode=kind == "array",
                    source_file=path,
                    marker_end=end,
                )
            )
    return structs, simple_to_full, all_aliases


def parse_members_for(structs, simple_to_full, aliases):
    """Parse each struct's body into members."""
    for si in structs:
        with open(si.source_file) as f:
            text = f.read()
        _, body_open = find_struct_after(text, si.marker_end)
        body, _ = struct_body_extent(text, body_open)
        for stmt in parse_struct_body(body):
            m = parse_member_stmt(stmt, simple_to_full, si.full_name, aliases)
            if m is not None:
                si.members.append(m)


def main():
    parser = argparse.ArgumentParser(description="json-dm code generator")
    parser.add_argument("--input", action="append", required=True, dest="inputs")
    parser.add_argument("--header", required=True)
    parser.add_argument("--source", required=True)
    args = parser.parse_args()

    structs, simple_to_full, aliases = scan_inputs(args.inputs)
    parse_members_for(structs, simple_to_full, aliases)

    # group structs by input file; one generated pair per input
    by_input = {}
    for si in structs:
        by_input.setdefault(si.source_file, []).append(si)

    for path, group in by_input.items():
        basename = path.rsplit("/", 1)[-1]
        stem = basename.rsplit(".", 1)[0]
        hpp_name = f"{stem}_jsondm.hpp"
        cpp_name = f"{stem}_jsondm.cpp"
        extra = set()
        for si in group:
            for m in si.members:
                refs = set()
                collect_user_types(m, refs)
                for simple in refs:
                    if simple == si.simple_name:
                        continue
                    for other in structs:
                        if other.simple_name == simple and other.source_file != path:
                            extra.add(
                                other.source_file.rsplit("/", 1)[-1].rsplit(".", 1)[0]
                                + "_jsondm.hpp"
                            )
        hpp = generate_hpp(group, basename)
        cpp = generate_cpp(group, hpp_name, extra)
        with open(f"{args.header}/{hpp_name}", "w") as f:
            f.write(hpp)
        with open(f"{args.source}/{cpp_name}", "w") as f:
            f.write(cpp)
        print(f"Generated {args.header}/{hpp_name}")
        print(f"Generated {args.source}/{cpp_name}")


if __name__ == "__main__":
    try:
        main()
    except ParseError as e:
        print(f"json-dm generation error: {e}", file=sys.stderr)
        sys.exit(1)

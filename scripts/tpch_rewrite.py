#!/usr/bin/env python3
"""Section 1 rewriter for TPC-H Rel queries.

Applies semantics-preserving source rewrites so the queries parse in
`RelParser.g4`:
  1a strip `@vectorized` / `@inline` annotations
  1b substitute `@@N` with concrete TPC-H parameter values
  1c rename `mean[` to `average[` (Q1 only)
  1d cap `decimal[64, …]` / `parse_decimal[64, …]` precision to 32 (DuckDB max 38)
  1f rewrite `(x.y.z)` and `app[x].y` to nested applications `y[x]`, `z[y[x]]`

The script concatenates `tpch_common_defs.rel` with a target query, applies
the rewrites, and prints the result.  It is intentionally regex-based and
limited to the patterns used in the benchmark queries; it is not a generic
Rel reformatter.

Usage:
  scripts/tpch_rewrite.py <query_number>             # print rewritten source
  scripts/tpch_rewrite.py <query_number> --no-defs   # query alone
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
QUERIES_DIR = ROOT / "benchmarks/TPCH/rel/queries"
COMMON_DEFS = ROOT / "benchmarks/TPCH/rel/tpch_common_defs.rel"


# Default TPC-H parameter values (from the official spec / qgen example output).
# `None` placeholders are unused for that particular query.
PARAMS: dict[int, list[object]] = {
    1: [90],
    2: [15, "BRASS", "EUROPE"],
    3: ["BUILDING", "1995-03-15"],
    4: ["1993-07-01", "1993-10-01"],
    5: ["ASIA", "1994-01-01", "1995-01-01"],
    6: ["1994-01-01", "1995-01-01", 0.06, 24],
    7: ["FRANCE", "GERMANY"],
    8: ["BRAZIL", "AMERICA", "ECONOMY ANODIZED STEEL"],
    9: ["green"],
    10: ["1993-10-01", "1994-01-01"],
    11: ["GERMANY", 0.0001],
    12: ["MAIL", "SHIP", "1994-01-01", "1995-01-01"],
    13: ["special", "requests"],
    14: ["1995-09-01", "1995-10-01"],
    15: ["1996-01-01", "1996-04-01"],
    16: ["Brand#45", "MEDIUM POLISHED", 49, 14, 23, 45, 19, 3, 36, 9],
    17: ["Brand#23", "MED BOX"],
    18: [300],
    19: ["Brand#12", "Brand#23", "Brand#34", 1, 10, 20],
    20: ["forest", "1994-01-01", "1995-01-01", "CANADA"],
    21: ["SAUDI ARABIA"],
    22: ["13", "31", "23", "29", "30", "18", "17"],
}


# Section 1a -----------------------------------------------------------------

# Match `@inline` / `@vectorized` and any whitespace that follows (including
# trailing newline when the annotation sits on its own line).  We deliberately
# do *not* swallow the rest of the line because TPC-H queries write
# `@inline def foo[...]: ...` on a single line.
ANNOTATION_RE = re.compile(r"@(?:vectorized|inline)\b[ \t]*\n?")


def strip_annotations(text: str) -> str:
    return ANNOTATION_RE.sub("", text)


# Section 1b -----------------------------------------------------------------


def substitute_params(text: str, q: int) -> str:
    params = PARAMS.get(q, [])
    # Replace the longest `@@N` first to avoid `@@10` colliding with `@@1`.
    for i in sorted(range(len(params)), reverse=True):
        token = f"@@{i + 1}"
        value = params[i]
        if isinstance(value, str):
            replacement = value
        else:
            replacement = str(value)
        text = text.replace(token, replacement)
    return text


# Section 1c -----------------------------------------------------------------


def rename_mean(text: str) -> str:
    return re.sub(r"\bmean\[", "average[", text)


# Section 1d -----------------------------------------------------------------

# TPC-H Rel uses decimal[64, 4] (RAI convention). DuckDB rejects DECIMAL width > 38.
DUCKDB_DECIMAL_MAX_PRECISION = 32

DECIMAL_HIGH_PRECISION_RE = re.compile(
    r"\b(decimal|parse_decimal)\s*\[\s*(\d+)\s*,\s*(\d+)",
    re.IGNORECASE,
)


def cap_decimal_precision(text: str, max_precision: int = DUCKDB_DECIMAL_MAX_PRECISION) -> str:
    """Lower decimal / parse_decimal precision when it exceeds DuckDB limits."""

    def repl(match: re.Match[str]) -> str:
        name = match.group(1)
        prec = int(match.group(2))
        scale = match.group(3)
        if prec <= max_precision:
            return match.group(0)
        return f"{name}[{max_precision}, {scale}"

    return DECIMAL_HIGH_PRECISION_RE.sub(repl, text)


# Section 1f -----------------------------------------------------------------

# Rewrite the dot-access sugar used in TPC-H queries:
#   - `a.b`              -> `b[a]`
#   - `a.b.c`            -> `c[b[a]]`
#   - `app[..].b`        -> `b[app[..]]`
#   - `( expr ).b`       -> `b[expr]`
# The TPC-H queries use these forms only inside parenthesised groups, so we
# repeatedly apply two replacements until the file stabilises.

DOT_AFTER_BRACKETS_RE = re.compile(r"(\]\s*)\.([A-Za-z_][A-Za-z0-9_]*)")
DOT_AFTER_PAREN_RE = re.compile(r"\)\s*\.([A-Za-z_][A-Za-z0-9_]*)")
DOT_AFTER_IDENT_RE = re.compile(
    r"([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)(?=[^A-Za-z0-9_])"
)


def _bracket_replace(match: re.Match[str]) -> str:
    return f"{match.group(1)} /*dot*/ .{match.group(2)}"


def _matching_paren_start(text: str, close_idx: int) -> int | None:
    """Given the index of a `)`, return the index of the matching `(` or None."""
    depth = 0
    for i in range(close_idx, -1, -1):
        ch = text[i]
        if ch == ")":
            depth += 1
        elif ch == "(":
            depth -= 1
            if depth == 0:
                return i
    return None


def _rewrite_paren_dot(text: str) -> tuple[str, bool]:
    """Rewrite a single occurrence of `(expr).field` -> `field[expr]`."""
    m = DOT_AFTER_PAREN_RE.search(text)
    if not m:
        return text, False
    close_idx = m.start()  # position of `)`
    open_idx = _matching_paren_start(text, close_idx)
    if open_idx is None:
        return text, False
    inner = text[open_idx + 1 : close_idx].strip()
    field = m.group(1)
    end = m.end()
    new = f"{field}[{inner}]"
    return text[:open_idx] + new + text[end:], True


DOT_FIELD_RE = re.compile(r"\.([A-Za-z_][A-Za-z0-9_]*)")


def _rewrite_bracket_dot(text: str) -> tuple[str, bool]:
    """Rewrite a single occurrence of `app[..].field` -> `field[app[..]]`."""
    pos = 0
    while True:
        m = DOT_FIELD_RE.search(text, pos)
        if not m:
            return text, False
        idx = m.start()
        j = idx - 1
        while j >= 0 and text[j].isspace():
            j -= 1
        if j < 0 or text[j] != "]":
            pos = idx + 1
            continue
        depth = 0
        start = None
        for k in range(j, -1, -1):
            if text[k] == "]":
                depth += 1
            elif text[k] == "[":
                depth -= 1
                if depth == 0:
                    start = k
                    break
        if start is None:
            pos = idx + 1
            continue
        head_end = start
        head_start = head_end - 1
        while head_start >= 0 and (text[head_start].isalnum() or text[head_start] == "_"):
            head_start -= 1
        head_start += 1
        head = text[head_start:head_end]
        if not head:
            pos = idx + 1
            continue
        bracket_body = text[start + 1 : j]
        field = m.group(1)
        end = m.end()
        new = f"{field}[{head}[{bracket_body}]]"
        return text[:head_start] + new + text[end:], True


def _rewrite_ident_dot(text: str) -> tuple[str, bool]:
    """Rewrite a single occurrence of `ident.field` -> `field[ident]`."""
    m = DOT_AFTER_IDENT_RE.search(text)
    if not m:
        return text, False
    ident, field = m.group(1), m.group(2)
    new = f"{field}[{ident}]"
    return text[: m.start()] + new + text[m.end() :], True


def rewrite_dot_access(text: str) -> str:
    # Apply repeatedly until no rule fires.
    for _ in range(64):
        changed = False
        text, c = _rewrite_paren_dot(text)
        changed |= c
        text, c = _rewrite_bracket_dot(text)
        changed |= c
        text, c = _rewrite_ident_dot(text)
        changed |= c
        if not changed:
            break
    return text


# Section 1j (sugar): wrap each def body in `{ ... }` so it matches the grammar's
#                     `relAbs` shape.  The TPC-H files extensively use the
#                     shorthand forms `def f[x]: expr`, `def f(x): formula`,
#                     and `def f[]: expr`; the grammar only accepts braced defs.
# --------------------------------------------------------------------------

BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")


def _strip_comments_for_scan(text: str) -> str:
    """Replace comments with whitespace so positions stay aligned."""
    def blank(m: re.Match[str]) -> str:
        return re.sub(r"[^\n]", " ", m.group(0))
    text = BLOCK_COMMENT_RE.sub(blank, text)
    text = LINE_COMMENT_RE.sub(blank, text)
    return text


DEF_HEADER_RE = re.compile(
    r"^[ \t]*def[ \t]+([A-Za-z_][A-Za-z0-9_]*)([\[\(\{:])",
    re.MULTILINE,
)


def _find_def_starts(scan: str) -> list[tuple[int, str, str]]:
    """Return (offset, def_name, kind) for each top-level def header.

    `kind` is one of `[`, `(`, `{`, `:` indicating what follows the name.
    """
    out: list[tuple[int, str, str]] = []
    for m in DEF_HEADER_RE.finditer(scan):
        out.append((m.start(), m.group(1), m.group(2)))
    return out


def _matching_close(text: str, open_idx: int, open_ch: str) -> int:
    pair = {"[": "]", "(": ")", "{": "}"}[open_ch]
    depth = 0
    for i in range(open_idx, len(text)):
        ch = text[i]
        if ch == open_ch:
            depth += 1
        elif ch == pair:
            depth -= 1
            if depth == 0:
                return i
    raise ValueError(f"unbalanced {open_ch} starting at {open_idx}")


def wrap_def_bodies(text: str) -> str:
    """Rewrite shorthand `def NAME[x]: body` to `def NAME { [x]: body }`."""
    scan = _strip_comments_for_scan(text)
    headers = _find_def_starts(scan)
    if not headers:
        return text
    # Append a sentinel end so the last body has a known terminator.
    pieces: list[str] = []
    cursor = 0
    for i, (start, name, kind) in enumerate(headers):
        pieces.append(text[cursor:start])
        # Compute the end of this def (the next def's start, or EOF).
        body_end = headers[i + 1][0] if i + 1 < len(headers) else len(text)
        header_segment_end = start
        # Find where the head (name + bracket/paren/colon) ends.
        if kind == "{":
            # Already braced; copy this def unchanged.
            close = _matching_close(text, scan.index("{", start), "{")
            pieces.append(text[start : close + 1])
            cursor = close + 1
            continue
        # Locate the `def NAME` head in the original text by re-running the regex.
        head_m = re.match(r"[ \t]*def[ \t]+([A-Za-z_][A-Za-z0-9_]*)", text[start:body_end])
        assert head_m is not None
        head_end = start + head_m.end()
        rest = text[head_end:body_end]
        if kind == ":":
            # `def NAME: body` -> `def NAME { body }`
            colon_pos = rest.find(":")
            body = rest[colon_pos + 1 :]
            pieces.append(f"{text[start:head_end]} {{{body.rstrip()}\n}}\n")
            cursor = body_end
            continue
        # kind is `[` or `(`
        opener_idx = scan.index(kind, head_end)
        close_idx = _matching_close(text, opener_idx, kind)
        bindings_segment = text[opener_idx : close_idx + 1]
        # Locate the colon after the bindings.
        after = text[close_idx + 1 : body_end]
        colon = after.find(":")
        if colon < 0:
            # No colon -> not a sugar form; keep verbatim.
            pieces.append(text[start:body_end])
            cursor = body_end
            continue
        body = after[colon + 1 :]
        # Empty bindings -> drop them entirely.
        inner_bindings = bindings_segment[1:-1].strip()
        if inner_bindings == "":
            pieces.append(f"{text[start:head_end]} {{{body.rstrip()}\n}}\n")
        else:
            pieces.append(
                f"{text[start:head_end]} {{ {bindings_segment}:{body.rstrip()}\n}}\n"
            )
        cursor = body_end
    pieces.append(text[cursor:])
    return "".join(pieces)


# --------------------------------------------------------------------------


def rewrite(query_num: int, include_defs: bool = True) -> str:
    query_path = QUERIES_DIR / f"{query_num}.rel"
    parts: list[str] = []
    if include_defs:
        parts.append(COMMON_DEFS.read_text())
    parts.append(query_path.read_text())
    text = "\n".join(parts)
    text = strip_annotations(text)
    text = substitute_params(text, query_num)
    text = rename_mean(text)
    text = cap_decimal_precision(text)
    text = rewrite_dot_access(text)
    text = wrap_def_bodies(text)
    return text


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Apply TPC-H Rel rewrites and print result.")
    p.add_argument("query", type=int, help="TPC-H query number (1-22)")
    p.add_argument("--no-defs", action="store_true", help="Skip prepending tpch_common_defs.rel")
    p.add_argument(
        "--list-params", action="store_true", help="Print the parameter mapping for the query and exit"
    )
    args = p.parse_args(argv)
    if args.list_params:
        for i, v in enumerate(PARAMS.get(args.query, []), start=1):
            print(f"@@{i} = {v!r}")
        return 0
    sys.stdout.write(rewrite(args.query, include_defs=not args.no_defs))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

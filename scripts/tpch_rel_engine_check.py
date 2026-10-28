#!/usr/bin/env python3
"""Check the Rel versions of the TPC-H queries against the reference SQL, on the Rel engine.

The counterpart of tpch_compare_results.py, which compares the translations with the reference: this
script runs the Rel version of each query itself on RAICode (the rel engine server, see
`task rel-engine:start`), over the wide relations of benchmarks/TPCH/rel/wide/tpch_wide_edb.edb filled
from the database, and compares its relation `result` with the answer of the reference SQL on the same
database. A query that fails here is not a faithful Rel version of the TPC-H query, whatever its
translation does.

The engine cannot load dates, so the date columns are loaded as strings into `<name>_in`, and a
prelude prepended to each program defines `<name>` from it with `parse_date`.

The answers are compared with the rules of tpch_compare_results.py (multisets of rows, leading columns
the reference does not output dropped, all-NULL reference rows ignored, numbers up to a relative
tolerance). The engine returns every value as a string: a column is read as numbers when the
reference's column is numeric.

Usage:
  scripts/tpch_rel_engine_check.py --db benchmarks/TPCH/data/tpch_sf001.duckdb            # all 22
  scripts/tpch_rel_engine_check.py --db ... --rel-dir benchmarks/TPCH/out/wide-rel 6 19
"""
from __future__ import annotations

import argparse
import datetime
import decimal
import importlib.util
import json
import pathlib
import socket
import sys

import duckdb

ROOT = pathlib.Path(__file__).resolve().parent.parent
REF_SQL_DIR = ROOT / "benchmarks/TPCH/sql"
WIDE_VIEWS = ROOT / "benchmarks/TPCH/rel/wide/wide_views.sql"
WIDE_EDB = ROOT / "benchmarks/TPCH/rel/wide/tpch_wide_edb.edb"
DEFAULT_REL_DIR = ROOT / "benchmarks/TPCH/out/wide-rel"
DEFAULT_SOCKET = ROOT / ".rel_engine.sock"

_spec = importlib.util.spec_from_file_location("tpch_compare_results", ROOT / "scripts/tpch_compare_results.py")
compare_results = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(compare_results)


def edb_cell(value):
    """A value as the engine reads it from JSON: dates as ISO strings, decimals as floats."""
    if isinstance(value, datetime.date):
        return value.isoformat()
    if isinstance(value, decimal.Decimal):
        return float(value)
    return value


def date_prelude(name: str, arity: int, date_columns: list[int]) -> str:
    """A Rel definition of `name` from `name_in`, whose date columns (0-based) are ISO strings."""
    args = [f"a{i + 1}" for i in range(arity)]
    raw = [f"s{i + 1}" if i in date_columns else args[i] for i in range(arity)]
    parses = " and ".join(f'a{i + 1} = parse_date[s{i + 1}, "Y-m-d"]' for i in date_columns)
    strings = ", ".join(f"s{i + 1}" for i in date_columns)
    return (f"def {name} {{ ({', '.join(args)}): exists(({strings}) | "
            f"{name}_in({', '.join(raw)}) and {parses}) }}\n")


def load_edb(con: duckdb.DuckDBPyConnection) -> tuple[dict[str, list[list]], str]:
    """The wide relations, as the engine loads them, and the prelude that restores their dates."""
    con.execute(WIDE_VIEWS.read_text())
    edb = {}
    prelude = ""
    for line in WIDE_EDB.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name = line.split()[0]
        cursor = con.execute(f"SELECT * FROM {name}")
        types = [str(d[1]) for d in cursor.description]
        rows = [[edb_cell(v) for v in row] for row in cursor.fetchall()]
        date_columns = [i for i, t in enumerate(types) if t.upper() == "DATE"]
        if date_columns:
            edb[f"{name}_in"] = rows
            prelude += date_prelude(name, len(types), date_columns)
        else:
            edb[name] = rows
    return edb, prelude


def run_on_engine(sock_path: pathlib.Path, program: str, edb: dict) -> dict:
    request = {"op": "run", "program": program, "output": "result", "output_arity": 0, "edb": edb, "raw": False}
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(str(sock_path))
        sock.sendall((json.dumps(request) + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = sock.recv(1 << 20)
            if not chunk:
                break
            buf += chunk
    return json.loads(buf)


def as_reference_type(value: str, ref_value):
    """A string from the engine, read as a number when the reference's value is one."""
    if isinstance(ref_value, (int, float, decimal.Decimal)) and not isinstance(ref_value, bool):
        return float(value)
    return value


def check(query: int, rel_dir: pathlib.Path, con, edb: dict, prelude: str,
          sock_path: pathlib.Path) -> tuple[bool, str]:
    rel_path = rel_dir / f"q{query}.rel"
    if not rel_path.exists():
        return False, f"missing {rel_path}"
    response = run_on_engine(sock_path, prelude + rel_path.read_text(), edb)
    if "error" in response:
        return False, f"engine error: {str(response['error']).splitlines()[0][:200]}"
    ref_raw = [row for row in con.execute((REF_SQL_DIR / f"q{query}.sql").read_text()).fetchall()
               if any(v is not None for v in row)]
    rel_raw = response.get("rows", [])

    ref_width = len(ref_raw[0]) if ref_raw else None
    rel_width = len(rel_raw[0]) if rel_raw else None
    dropped = 0
    if ref_width is not None and rel_width is not None and rel_width > ref_width:
        dropped = rel_width - ref_width
        rel_raw = [row[dropped:] for row in rel_raw]
    template = ref_raw[0] if ref_raw else ()
    try:
        rel = [tuple(compare_results.normalize(as_reference_type(v, t)) for v, t in zip(row, template)) if template
               else tuple(row) for row in rel_raw]
    except ValueError as exc:
        return False, f"value of a different type: {exc}"
    ref = [tuple(compare_results.normalize(v) for v in row) for row in ref_raw]
    ok = compare_results.multiset_equal(ref, rel)
    note = f"{len(ref)} rows" + (f", {dropped} leading column(s) dropped" if dropped else "")
    if not ok:
        note += f"; Rel has {len(rel)} rows"
    return ok, note


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("queries", nargs="*", type=int)
    ap.add_argument("--db", type=pathlib.Path, required=True)
    ap.add_argument("--rel-dir", type=pathlib.Path, default=DEFAULT_REL_DIR,
                    help="Read q<N>.rel from here (default: benchmarks/TPCH/out/wide-rel)")
    ap.add_argument("--socket", type=pathlib.Path, default=DEFAULT_SOCKET)
    args = ap.parse_args()
    queries = args.queries or list(range(1, 23))
    if not args.socket.exists():
        print(f"No rel engine server at {args.socket}: start it with `task rel-engine:start`", file=sys.stderr)
        return 1

    con = duckdb.connect(":memory:")
    con.execute(f"ATTACH '{args.db}' AS tpch (READ_ONLY)")
    con.execute("USE tpch")
    # The wide views go in the in-memory database, so the attached one stays read-only.
    con.execute("USE memory")
    con.execute("SET search_path = 'memory,tpch'")
    edb, prelude = load_edb(con)

    failures = 0
    for q in queries:
        ok, note = check(q, args.rel_dir, con, edb, prelude, args.socket)
        failures += not ok
        print(f"  [{'OK' if ok else 'DIFF'}] Q{q}: {note}", flush=True)
    print(f"\n{len(queries) - failures}/{len(queries)} Rel queries return the reference's values on the Rel engine")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

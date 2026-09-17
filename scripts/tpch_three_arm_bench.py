#!/usr/bin/env python3
"""EXPLAIN ANALYZE the same query three ways and report times plus ratios.

Arms:
  ref      benchmarks/TPCH/sql/q<N>.sql              (reference TPC-H SQL)
  current  benchmarks/TPCH/out/sql/q<N>.sql          (translated benchmarks/TPCH/rel/queries)
  ai       benchmarks/TPCH/out/ai-sql/q<N>.sql       (translated benchmarks/TPCH/rel/ai-rewrites)

Times come from DuckDB's own EXPLAIN ANALYZE latency, not wall clock, so process
startup is excluded.  A query returning zero rows is reported as `empty`: its
timing is meaningless (Q11 at SF1 is vacuous -- see PARAMS[11] in tpch_rewrite.py).

Usage:
  scripts/tpch_three_arm_bench.py                 # all 22
  scripts/tpch_three_arm_bench.py 1 12 --runs 5
"""

from __future__ import annotations

import argparse
import os
import pathlib
import statistics
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from tpch_explain_bench import (  # noqa: E402
    TIMEOUT_RC,
    explain_sql,
    run_duckdb_session,
    wall_time_seconds,
)

ROOT = pathlib.Path(__file__).resolve().parent.parent
REF_DIR = ROOT / "benchmarks/TPCH/sql"
CUR_DIR = ROOT / "benchmarks/TPCH/out/sql"
AI_DIR = ROOT / "benchmarks/TPCH/out/ai-sql"
RESULT_QUERY = "SELECT * FROM result"


def arm_paths(q: int) -> dict[str, tuple[pathlib.Path | None, str]]:
    ref = REF_DIR / f"q{q}.sql"
    return {
        "ref": (None, ref.read_text() if ref.is_file() else ""),
        "current": (CUR_DIR / f"q{q}.sql", RESULT_QUERY),
        "ai": (AI_DIR / f"q{q}.sql", RESULT_QUERY),
    }


def measure(db, setup_path, query_sql, *, runs, warmup, duckdb, timeout):
    """Median EXPLAIN ANALYZE latency, or a status string."""
    if setup_path is not None:
        if not setup_path.is_file():
            return "missing"
        setup = setup_path.read_text()
    else:
        setup = ""
    if not query_sql.strip():
        return "missing"

    # Row count first: a zero-row result makes the timing meaningless.
    objs, err, rc = run_duckdb_session(
        db, [setup, f"SELECT count(*) AS n FROM ({query_sql.strip().rstrip(';')}) t;"],
        duckdb=duckdb, json_output=True, timeout_sec=timeout,
    )
    if rc == TIMEOUT_RC:
        return "timeout"
    if rc != 0:
        return "error"
    rows = 0
    for o in objs:
        if isinstance(o, list) and o and isinstance(o[0], dict) and "n" in o[0]:
            rows = int(o[0]["n"])
    if rows == 0:
        return "empty"

    stmts = [setup] + [explain_sql(query_sql)] * (warmup + runs)
    objs, err, rc = run_duckdb_session(
        db, stmts, duckdb=duckdb, json_output=True, timeout_sec=timeout
    )
    if rc == TIMEOUT_RC:
        return "timeout"
    if rc != 0:
        return "error"
    times = []
    for o in objs:
        plan = o[0] if isinstance(o, list) and o else o
        if isinstance(plan, dict) and ("latency" in plan or "children" in plan):
            try:
                times.append(wall_time_seconds(plan))
            except Exception:
                pass
    times = times[warmup:] if len(times) > warmup else times
    return statistics.median(times) if times else "error"


def fmt_t(v):
    return f"{v:.4f}" if isinstance(v, float) else v


def ratio(a, b):
    """a relative to b, as a signed speed factor string."""
    if not isinstance(a, float) or not isinstance(b, float) or a <= 0 or b <= 0:
        return "-"
    return f"{b / a:.1f}x" if b >= a else f"{a / b:.1f}x"


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("queries", nargs="*", type=int)
    ap.add_argument("--db", type=pathlib.Path, default=None)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--duckdb", default="duckdb")
    ap.add_argument("--query-timeout-sec", type=float, default=120.0)
    a = ap.parse_args(argv)

    db = a.db or pathlib.Path(
        os.environ.get("TPCH_DUCKDB_PATH", ROOT / "benchmarks/TPCH/data/tpch_sf100.duckdb")
    )
    if not db.is_file():
        print(f"Missing DB {db}", file=sys.stderr)
        return 1
    qs = a.queries or list(range(1, 23))

    print(f"DB: {db}   runs={a.runs} warmup={a.warmup}   times = EXPLAIN ANALYZE latency (s)\n")
    hdr = f"{'Q':>4} {'ref':>9} {'current':>9} {'ai':>9}   {'cur/ref':>8} {'ai/ref':>8}   {'ai vs current':>14}"
    print(hdr)
    print("-" * len(hdr))
    rows = []
    for q in qs:
        arms = arm_paths(q)
        res = {
            name: measure(db, p, s, runs=a.runs, warmup=a.warmup,
                          duckdb=a.duckdb, timeout=a.query_timeout_sec)
            for name, (p, s) in arms.items()
        }
        r, c, ai = res["ref"], res["current"], res["ai"]
        verdict = "-"
        if isinstance(c, float) and isinstance(ai, float):
            verdict = f"{c/ai:.1f}x faster" if ai < c else f"{ai/c:.1f}x slower"
        elif c == "timeout" and isinstance(ai, float):
            verdict = "fixed (was TO)"
        elif isinstance(c, float) and ai == "timeout":
            verdict = "REGRESSED (TO)"
        print(f"{'Q'+str(q):>4} {fmt_t(r):>9} {fmt_t(c):>9} {fmt_t(ai):>9}   "
              f"{ratio(c,r):>8} {ratio(ai,r):>8}   {verdict:>14}")
        rows.append((q, r, c, ai))

    tr = sum(v for _, v, _, _ in rows if isinstance(v, float))
    tc = sum(v for _, _, v, _ in rows if isinstance(v, float))
    ta = sum(v for _, _, _, v in rows if isinstance(v, float))
    print("-" * len(hdr))
    print(f"{'SUM':>4} {tr:>9.4f} {tc:>9.4f} {ta:>9.4f}   "
          f"{tc/tr:>7.1f}x {ta/tr:>7.1f}x   {tc/ta:>9.1f}x faster")
    print("\n(sums cover only queries where all arms produced a time)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Time the TPC-H reference queries and their translations interleaved, over independent sessions.

tpch_explain_bench.py times each side in a batch of its own, through EXPLAIN ANALYZE. This script is
the one behind the numbers of the paper, and avoids both:

  * each round runs the reference query and every translation arm once, in a random order, so that a
    change in the state of the machine affects all of them alike;
  * a query is timed by materializing its full result inside DuckDB
    (CREATE OR REPLACE TEMP TABLE ... AS <query>), without profiling and without converting the rows
    to Python;
  * each session is a separate process with fresh connections, and the sessions are summarized
    together: per query, the median over sessions of the per-session medians, and the range of the
    per-session slowdowns.

The reference query is timed without a final ORDER BY that has no LIMIT (see --unordered-ref of
tpch_explain_bench.py): Rel relations are sets, and the translations do not sort.

The DuckDB version is that of the Python package running the script, and is recorded in the output.

Every side works on its own copy of the database, since the translations of different arms create
views of the same names.

Usage:
  scripts/tpch_interleaved_bench.py --db sf1.duckdb \\
      --arm nokeys=benchmarks/TPCH/out/wide-nokeys-sql --arm keys=benchmarks/TPCH/out/wide-sql \\
      --sessions 5 --rounds 15 --out results.json
"""
from __future__ import annotations

import argparse
import json
import pathlib
import random
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

import duckdb

ROOT = pathlib.Path(__file__).resolve().parent.parent
REF_SQL_DIR = ROOT / "benchmarks/TPCH/sql"
sys.path.insert(0, str(ROOT / "scripts"))
from tpch_explain_bench import strip_unlimited_order_by  # noqa: E402


def timed(con: duckdb.DuckDBPyConnection, query: str) -> float:
    start = time.perf_counter()
    con.execute(f"CREATE OR REPLACE TEMP TABLE bench_out AS {query}")
    return time.perf_counter() - start


def run_session(args) -> dict:
    """One session: per query and side, the times of the measured rounds."""
    arms = dict(a.split("=", 1) for a in args.arm)
    rng = random.Random(args.seed)
    copies = {side: pathlib.Path(args.work_dir) / f"{side}.duckdb" for side in ["ref", *arms]}
    cons = {side: duckdb.connect(str(path)) for side, path in copies.items()}
    times: dict[str, dict[str, list[float]]] = {}
    for q in args.queries:
        queries = {"ref": strip_unlimited_order_by((REF_SQL_DIR / f"q{q}.sql").read_text()).strip().rstrip(";")}
        for side, sql_dir in arms.items():
            cons[side].execute((pathlib.Path(sql_dir) / f"q{q}.sql").read_text())
            queries[side] = "SELECT * FROM result"
        sides = list(queries)
        for _ in range(args.warmup):
            for side in sides:
                timed(cons[side], queries[side])
        per_side = {side: [] for side in sides}
        for _ in range(args.rounds):
            rng.shuffle(sides)
            for side in sides:
                per_side[side].append(timed(cons[side], queries[side]))
        times[str(q)] = per_side
    for con in cons.values():
        con.close()
    return times


def summarize(sessions: list[dict], arms: list[str]) -> dict:
    summary = {}
    for q in sessions[0]:
        ref_medians = [statistics.median(s[q]["ref"]) for s in sessions]
        row = {"ref_ms": statistics.median(ref_medians) * 1000}
        for arm in arms:
            gen_medians = [statistics.median(s[q][arm]) for s in sessions]
            slowdowns = [g / r for g, r in zip(gen_medians, ref_medians)]
            row[arm] = {
                "ms": statistics.median(gen_medians) * 1000,
                "slowdown": statistics.median(slowdowns),
                "slowdown_min": min(slowdowns),
                "slowdown_max": max(slowdowns),
            }
        summary[q] = row
    return summary


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("queries", nargs="*", type=int)
    ap.add_argument("--db", type=pathlib.Path, required=True)
    ap.add_argument("--arm", action="append", required=True, help="name=dir of translated q<N>.sql scripts")
    ap.add_argument("--sessions", type=int, default=5)
    ap.add_argument("--rounds", type=int, default=15)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--out", type=pathlib.Path, required=True, help="JSON file for the raw times and the summary")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--one-session", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--work-dir", help=argparse.SUPPRESS)
    args = ap.parse_args()
    args.queries = args.queries or list(range(1, 23))

    if args.one_session:
        json.dump(run_session(args), sys.stdout)
        return 0

    arms = [a.split("=", 1)[0] for a in args.arm]
    sessions = []
    with tempfile.TemporaryDirectory() as work:
        for side in ["ref", *arms]:
            shutil.copy(args.db, pathlib.Path(work) / f"{side}.duckdb")
        for i in range(args.sessions):
            cmd = [sys.executable, __file__, *map(str, args.queries), "--db", str(args.db), "--one-session",
                   "--work-dir", work, "--rounds", str(args.rounds), "--warmup", str(args.warmup),
                   "--seed", str(args.seed + i), "--out", str(args.out)]
            for a in args.arm:
                cmd += ["--arm", a]
            print(f"session {i + 1}/{args.sessions}...", file=sys.stderr, flush=True)
            sessions.append(json.loads(subprocess.run(cmd, check=True, capture_output=True, text=True).stdout))

    summary = summarize(sessions, arms)
    args.out.write_text(json.dumps({"duckdb": duckdb.__version__, "sessions": sessions, "summary": summary,
                                    "rounds": args.rounds, "warmup": args.warmup}, indent=1))
    print(f"DuckDB {duckdb.__version__}, {args.sessions} sessions of {args.rounds} rounds after {args.warmup} warm-up")
    header = f"{'Q':>4} {'ref ms':>8}" + "".join(f" {arm + ' x':>10} {'range':>13}" for arm in arms)
    print(header)
    for q, row in summary.items():
        line = f"{'Q' + q:>4} {row['ref_ms']:8.1f}"
        for arm in arms:
            r = row[arm]
            line += f" {r['slowdown']:10.2f} {r['slowdown_min']:6.2f}-{r['slowdown_max']:<6.2f}"
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Multi-run EXPLAIN (ANALYZE, FORMAT JSON) for reference vs rel2sql TPC-H SQL.

Requires TPCH_DUCKDB_PATH (or --db) with data from scripts/tpch_build_duckdb.py.
Writes artifacts under benchmarks/TPCH/out/explain/q{N}/.
Re-running a query removes that query's previous output directory first.

Example:
  export TPCH_DUCKDB_PATH="$(pwd)/benchmarks/TPCH/data/tpch_sf100.duckdb"
  task tpch:emit-sql -- 18
  python3 scripts/tpch_explain_bench.py 18 --runs 10 --warmup 2
"""
from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import shutil
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from typing import Any

ROOT = pathlib.Path(__file__).resolve().parent.parent
REF_SQL_DIR = ROOT / "benchmarks/TPCH/sql"
GEN_SQL_DIR = ROOT / "benchmarks/TPCH/out/sql"
OUT_EXPLAIN = ROOT / "benchmarks/TPCH/out/explain"
EMIT_SCRIPT = ROOT / "scripts/tpch_emit_sql.sh"

GEN_RESULT_QUERY = "SELECT * FROM result"


def strip_sql_semicolon(sql: str) -> str:
    s = sql.strip()
    if s.endswith(";"):
        s = s[:-1].rstrip()
    return s


def explain_sql(query_sql: str) -> str:
    body = strip_sql_semicolon(query_sql)
    return f"EXPLAIN (ANALYZE, FORMAT JSON) {body};"


def parse_json_stream(text: str) -> list[Any]:
    decoder = json.JSONDecoder()
    objs: list[Any] = []
    idx = 0
    while idx < len(text):
        rest = text[idx:].lstrip()
        if not rest:
            break
        obj, end = decoder.raw_decode(rest)
        objs.append(obj)
        idx += len(text[idx:]) - len(rest) + end
    return objs


def run_duckdb_session(
    db_path: pathlib.Path,
    statements: list[str],
    *,
    duckdb: str,
    json_output: bool,
) -> tuple[list[Any], str, int]:
    """Run statements on one DuckDB connection; return parsed JSON objects if json_output."""
    cmd = [duckdb, str(db_path)]
    if json_output:
        cmd.append("-json")
    proc = subprocess.run(
        cmd,
        input="\n".join(statements) + "\n",
        capture_output=True,
        text=True,
    )
    stderr = proc.stderr or ""
    if proc.returncode != 0:
        return [], stderr, proc.returncode
    if not json_output:
        return [], stderr, 0
    try:
        return parse_json_stream(proc.stdout), stderr, 0
    except json.JSONDecodeError as e:
        return [], f"JSON parse error: {e}\nstdout:\n{proc.stdout[:2000]}", 1


def wall_time_seconds(plan: dict[str, Any]) -> float:
    latency = plan.get("latency")
    if latency is not None and float(latency) > 0:
        return float(latency)

    def sum_timing(node: dict[str, Any]) -> float:
        total = float(node.get("operator_timing") or 0)
        for ch in node.get("children") or []:
            if isinstance(ch, dict):
                total += sum_timing(ch)
        return total

    return sum_timing(plan)


def collect_operator_types(node: dict[str, Any], out: list[str]) -> None:
    op = node.get("operator_type")
    if op:
        out.append(str(op))
    for ch in node.get("children") or []:
        if isinstance(ch, dict):
            collect_operator_types(ch, out)


def plan_execution_root(plan: dict[str, Any]) -> dict[str, Any]:
    """Strip profiling/EXPLAIN wrappers to reach the query physical plan root."""
    node = plan
    for _ in range(16):
        op = node.get("operator_type")
        if op and op not in ("EXPLAIN_ANALYZE", "EXPLAIN"):
            return node
        children = node.get("children") or []
        if len(children) == 1 and isinstance(children[0], dict):
            node = children[0]
            continue
        return node
    return node


def plan_fingerprint(plan: dict[str, Any], *, execution_only: bool = False) -> list[str]:
    root = plan_execution_root(plan) if execution_only else plan
    ops: list[str] = []
    collect_operator_types(root, ops)
    return ops


def format_plan_tree(node: dict[str, Any], depth: int = 0, lines: list[str] | None = None) -> list[str]:
    if lines is None:
        lines = []
    op = node.get("operator_type") or node.get("operator_name") or "?"
    timing = node.get("operator_timing")
    card = node.get("operator_cardinality")
    parts = [f"{'  ' * depth}{op}"]
    if timing is not None:
        parts.append(f"timing={float(timing):.6f}s")
    if card is not None:
        parts.append(f"rows={card}")
    extra = node.get("extra_info") or {}
    if extra:
        brief = ", ".join(f"{k}={v!r}" for k, v in list(extra.items())[:4])
        if len(extra) > 4:
            brief += ", ..."
        parts.append(f"({brief})")
    lines.append(" ".join(parts))
    for ch in node.get("children") or []:
        if isinstance(ch, dict):
            format_plan_tree(ch, depth + 1, lines)
    return lines


def is_join_operator(op: str) -> bool:
    return "JOIN" in op or op == "CROSS_PRODUCT"


def filter_join_operators(ops: list[str]) -> list[str]:
    return [op for op in ops if is_join_operator(op)]


def format_operator_diff(ref_ops: list[str], gen_ops: list[str]) -> list[str]:
    """Side-by-side pre-order operator list; marks first mismatch."""
    lines = ["  #  ref                          gen", "  -- ------------------------------ ------------------------------"]
    n = max(len(ref_ops), len(gen_ops))
    if n == 0:
        return ["  (none)"]
    div = None
    for i in range(n):
        r = ref_ops[i] if i < len(ref_ops) else ""
        g = gen_ops[i] if i < len(gen_ops) else ""
        if div is None and r != g:
            div = i
        mark = " !" if div == i else "  "
        lines.append(f"{mark}{i:3d}  {r:30} {g:30}")
    if div is not None:
        lines.append(f"  First difference at index {div}.")
    elif ref_ops == gen_ops and ref_ops:
        lines.append("  Sequences match.")
    return lines


def compare_fingerprints(ref: list[str], gen: list[str]) -> dict[str, Any]:
    match = ref == gen
    divergence_index: int | None = None
    if not match:
        for i in range(max(len(ref), len(gen))):
            r = ref[i] if i < len(ref) else None
            g = gen[i] if i < len(gen) else None
            if r != g:
                divergence_index = i
                break
    return {
        "plans_match": match,
        "ref_operator_count": len(ref),
        "gen_operator_count": len(gen),
        "divergence_index": divergence_index,
        "ref_ops_preview": ref[:20],
        "gen_ops_preview": gen[:20],
    }


def timing_stats(values: list[float]) -> dict[str, float]:
    if not values:
        return {}
    sorted_v = sorted(values)
    n = len(sorted_v)
    p95_idx = min(n - 1, max(0, int(math.ceil(0.95 * n)) - 1))
    return {
        "count": float(n),
        "min": min(sorted_v),
        "max": max(sorted_v),
        "mean": statistics.mean(sorted_v),
        "median": statistics.median(sorted_v),
        "stdev": statistics.stdev(sorted_v) if n > 1 else 0.0,
        "p95": sorted_v[p95_idx],
    }


def clear_query_output_dir(out_dir: pathlib.Path) -> None:
    """Remove prior benchmark artifacts so a re-run does not mix old and new runs."""
    if out_dir.exists():
        shutil.rmtree(out_dir)


def ensure_translated_sql(query: int, *, duckdb: str) -> pathlib.Path:
    path = GEN_SQL_DIR / f"q{query}.sql"
    if path.is_file():
        return path
    if not EMIT_SCRIPT.is_file():
        raise FileNotFoundError(f"Missing translated SQL {path} and {EMIT_SCRIPT}")
    print(f"Emitting SQL for Q{query}...", file=sys.stderr)
    subprocess.run(
        [str(EMIT_SCRIPT), str(query)],
        cwd=ROOT,
        check=True,
    )
    if not path.is_file():
        raise FileNotFoundError(f"Emit did not create {path}")
    return path


def run_plans(
    db_path: pathlib.Path,
    explain_stmts: list[str],
    *,
    setup_sql: str | None,
    duckdb: str,
    cold: bool,
) -> list[dict[str, Any]]:
    if cold:
        plans: list[dict[str, Any]] = []
        for stmt in explain_stmts:
            prefix = [setup_sql] if setup_sql else []
            objs, err, rc = run_duckdb_session(
                db_path, prefix + [stmt], duckdb=duckdb, json_output=True
            )
            if rc != 0:
                raise RuntimeError(f"explain failed: {err}")
            if not objs:
                raise RuntimeError("explain returned no JSON")
            plans.append(objs[-1])
        return plans

    objs, err, rc = run_duckdb_session(db_path, explain_stmts, duckdb=duckdb, json_output=True)
    if rc != 0:
        raise RuntimeError(f"session failed: {err}")
    if len(objs) != len(explain_stmts):
        raise RuntimeError(f"expected {len(explain_stmts)} plans, got {len(objs)}: {err}")
    return [o for o in objs if isinstance(o, dict)]


def run_arm(
    arm: str,
    db_path: pathlib.Path,
    *,
    setup_sql: str | None,
    query_sql: str,
    warmup: int,
    runs: int,
    duckdb: str,
    cold: bool,
) -> dict[str, Any]:
    explain_stmt = explain_sql(query_sql)
    setup_ms: float | None = None

    if setup_sql and not cold:
        t0 = time.perf_counter()
        _, err, rc = run_duckdb_session(db_path, [setup_sql], duckdb=duckdb, json_output=False)
        setup_ms = (time.perf_counter() - t0) * 1000
        if rc != 0:
            raise RuntimeError(f"{arm} setup failed: {err}")

    if warmup > 0:
        run_plans(
            db_path,
            [explain_stmt] * warmup,
            setup_sql=setup_sql if cold else None,
            duckdb=duckdb,
            cold=cold,
        )

    measured_plans = run_plans(
        db_path,
        [explain_stmt] * runs,
        setup_sql=setup_sql if cold else None,
        duckdb=duckdb,
        cold=cold,
    )

    times = [wall_time_seconds(p) for p in measured_plans]
    order = sorted(range(len(times)), key=lambda i: times[i])
    median_run_idx = order[len(order) // 2]

    return {
        "arm": arm,
        "setup_ms": setup_ms,
        "wall_time_seconds": times,
        "timing": timing_stats(times),
        "median_run_index": median_run_idx,
        "plans": measured_plans,
    }


def git_sha() -> str | None:
    try:
        proc = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        return proc.stdout.strip() or None
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def write_summary_txt(
    path: pathlib.Path,
    summary: dict[str, Any],
    *,
    ref_median_plan: dict[str, Any],
    gen_median_plan: dict[str, Any],
) -> None:
    ref_root = plan_execution_root(ref_median_plan)
    gen_root = plan_execution_root(gen_median_plan)
    ref_ops = plan_fingerprint(ref_median_plan, execution_only=True)
    gen_ops = plan_fingerprint(gen_median_plan, execution_only=True)

    warmup = summary.get("warmup", 0)
    runs = summary.get("runs", 0)
    lines = [
        f"Query Q{summary['query']}",
        f"Database: {summary['db_path']}",
        f"Timestamp: {summary['timestamp']}",
        f"Warmup: {warmup}  Measured runs: {runs}  (per arm)",
        "",
        "Timing (EXPLAIN ANALYZE, median run):",
    ]
    for arm in ("ref", "gen"):
        t = summary["arms"][arm]["timing"]
        lines.append(
            f"  {arm}: median={t.get('median', 0):.4f}s "
            f"min={t.get('min', 0):.4f}s max={t.get('max', 0):.4f}s "
            f"p95={t.get('p95', 0):.4f}s"
        )
    fp = summary["plan_comparison"]
    lines.extend(
        [
            "",
            "Plan comparison (pre-order operator_type, execution plan only):",
            f"  Match: {fp['plans_match']}  ref_ops={len(ref_ops)}  gen_ops={len(gen_ops)}",
            "",
            "Operator sequence diff:",
            *format_operator_diff(ref_ops, gen_ops),
            "",
        ]
    )
    ref_joins = filter_join_operators(ref_ops)
    gen_joins = filter_join_operators(gen_ops)
    lines.extend(
        [
            "Join operator sequence diff (pre-order; *_JOIN and CROSS_PRODUCT only):",
            f"  ref_joins={len(ref_joins)}  gen_joins={len(gen_joins)}  match={ref_joins == gen_joins}",
            "",
            *format_operator_diff(ref_joins, gen_joins),
            "",
            "=" * 72,
            "REFERENCE plan (median run)",
            "=" * 72,
            *format_plan_tree(ref_root),
            "",
            "=" * 72,
            "TRANSLATED plan (median run; SELECT * FROM result)",
            "=" * 72,
            *format_plan_tree(gen_root),
            "",
            "Full JSON plans: ref/plan_median.json  gen/plan_median.json",
            "",
            "Note: DuckDB parallel execution can make sum(operator_timing) > wall latency.",
        ]
    )
    path.write_text("\n".join(lines) + "\n")


def maybe_graphviz(
    db_path: pathlib.Path,
    query_sql: str,
    out_path: pathlib.Path,
    *,
    duckdb: str,
    setup_sql: str | None = None,
) -> None:
    stmt = f"EXPLAIN (ANALYZE, FORMAT GRAPHVIZ) {strip_sql_semicolon(query_sql)};"
    stmts = ([setup_sql] if setup_sql else []) + [stmt]
    proc = subprocess.run(
        [duckdb, str(db_path)],
        input="\n".join(stmts) + "\n",
        capture_output=True,
        text=True,
    )
    if proc.returncode == 0 and proc.stdout.strip():
        out_path.write_text(proc.stdout)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("queries", nargs="+", type=int, help="TPC-H query number(s), e.g. 18")
    ap.add_argument("--db", type=pathlib.Path, default=None, help="DuckDB file (default: TPCH_DUCKDB_PATH)")
    ap.add_argument("--warmup", type=int, default=2, help="Warmup EXPLAIN ANALYZE runs (discarded)")
    ap.add_argument("--runs", type=int, default=10, help="Measured runs per arm")
    ap.add_argument("--duckdb", default="duckdb", help="duckdb executable")
    ap.add_argument("--cold", action="store_true", help="Re-open DB each run (experimental)")
    ap.add_argument("--graphviz", action="store_true", help="Write GRAPHVIZ plans for median runs")
    ap.add_argument("--out-root", type=pathlib.Path, default=OUT_EXPLAIN, help="Output root directory")
    args = ap.parse_args()

    db_env = os.environ.get("TPCH_DUCKDB_PATH", "")
    db_path = (args.db or pathlib.Path(db_env) if db_env else None)
    if db_path is None:
        print("Set TPCH_DUCKDB_PATH or pass --db", file=sys.stderr)
        return 1
    db_path = db_path.resolve()
    if not db_path.is_file():
        print(f"Missing database: {db_path}", file=sys.stderr)
        return 1

    failures = 0
    for query in args.queries:
        ref_path = REF_SQL_DIR / f"q{query}.sql"
        if not ref_path.is_file():
            print(f"Missing reference SQL: {ref_path}", file=sys.stderr)
            failures += 1
            continue

        try:
            gen_path = ensure_translated_sql(query, duckdb=args.duckdb)
        except (FileNotFoundError, subprocess.CalledProcessError) as e:
            print(f"Q{query}: {e}", file=sys.stderr)
            failures += 1
            continue

        ref_sql = ref_path.read_text()
        gen_script = gen_path.read_text()

        out_dir = args.out_root / f"q{query}"
        clear_query_output_dir(out_dir)
        ref_dir = out_dir / "ref"
        gen_dir = out_dir / "gen"
        ref_dir.mkdir(parents=True, exist_ok=True)
        gen_dir.mkdir(parents=True, exist_ok=True)

        print(f"Q{query}: reference arm ({args.runs} runs)...", file=sys.stderr)
        try:
            ref_result = run_arm(
                "ref",
                db_path,
                setup_sql=None,
                query_sql=ref_sql,
                warmup=args.warmup,
                runs=args.runs,
                duckdb=args.duckdb,
                cold=args.cold,
            )
        except RuntimeError as e:
            print(f"Q{query} ref failed: {e}", file=sys.stderr)
            failures += 1
            continue

        print(f"Q{query}: translated arm ({args.runs} runs)...", file=sys.stderr)
        try:
            gen_result = run_arm(
                "gen",
                db_path,
                setup_sql=gen_script,
                query_sql=GEN_RESULT_QUERY,
                warmup=args.warmup,
                runs=args.runs,
                duckdb=args.duckdb,
                cold=args.cold,
            )
        except RuntimeError as e:
            print(f"Q{query} gen failed: {e}", file=sys.stderr)
            failures += 1
            continue

        for i, plan in enumerate(ref_result["plans"]):
            (ref_dir / f"run_{i:03d}.json").write_text(json.dumps(plan, indent=2) + "\n")
        for i, plan in enumerate(gen_result["plans"]):
            (gen_dir / f"run_{i:03d}.json").write_text(json.dumps(plan, indent=2) + "\n")

        ref_median = ref_result["plans"][ref_result["median_run_index"]]
        gen_median = gen_result["plans"][gen_result["median_run_index"]]
        (ref_dir / "plan_median.json").write_text(json.dumps(ref_median, indent=2) + "\n")
        (gen_dir / "plan_median.json").write_text(json.dumps(gen_median, indent=2) + "\n")

        ref_fp = plan_fingerprint(ref_median, execution_only=True)
        gen_fp = plan_fingerprint(gen_median, execution_only=True)
        plan_cmp = compare_fingerprints(ref_fp, gen_fp)
        plan_cmp["ref_operator_sequence"] = ref_fp
        plan_cmp["gen_operator_sequence"] = gen_fp

        summary: dict[str, Any] = {
            "query": query,
            "db_path": str(db_path),
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "git_sha": git_sha(),
            "warmup": args.warmup,
            "runs": args.runs,
            "cold": args.cold,
            "unoptimized": os.environ.get("UNOPTIMIZED") == "1",
            "arms": {
                "ref": {
                    "sql_file": str(ref_path.relative_to(ROOT)),
                    "query": strip_sql_semicolon(ref_sql),
                    "timing": ref_result["timing"],
                    "median_run_index": ref_result["median_run_index"],
                },
                "gen": {
                    "sql_file": str(gen_path.relative_to(ROOT)),
                    "setup_sql_file": str(gen_path.relative_to(ROOT)),
                    "query": GEN_RESULT_QUERY,
                    "setup_ms": gen_result.get("setup_ms"),
                    "timing": gen_result["timing"],
                    "median_run_index": gen_result["median_run_index"],
                },
            },
            "plan_comparison": plan_cmp,
        }

        (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        write_summary_txt(
            out_dir / "summary.txt",
            summary,
            ref_median_plan=ref_median,
            gen_median_plan=gen_median,
        )

        if args.graphviz:
            maybe_graphviz(db_path, ref_sql, ref_dir / "plan_median.dot", duckdb=args.duckdb)
            maybe_graphviz(
                db_path,
                GEN_RESULT_QUERY,
                gen_dir / "plan_median.dot",
                duckdb=args.duckdb,
                setup_sql=gen_script,
            )

        rt = summary["arms"]["ref"]["timing"]
        gt = summary["arms"]["gen"]["timing"]
        print(
            f"Q{query}: ref median={rt['median']:.4f}s gen median={gt['median']:.4f}s "
            f"plans_match={plan_cmp['plans_match']} -> {out_dir}",
            file=sys.stderr,
        )

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

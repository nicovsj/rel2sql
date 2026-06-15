#!/usr/bin/env python3
"""Aggregate TPC-H EXPLAIN ANALYZE benchmarks into a CSV summary.

Reads benchmarks/TPCH/out/explain/q{N}/summary.json (from tpch_explain_bench.py)
and benchmarks/TPCH/pipeline/manifest.json (translate: ok = explainable).

Example:
  python3 scripts/tpch_explain_summary_csv.py
  python3 scripts/tpch_explain_summary_csv.py -o benchmarks/TPCH/out/explain/summary.csv
  task tpch:explain-summary
"""
from __future__ import annotations

import argparse
import csv
import json
import pathlib
import re
import sys
from typing import Any

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_EXPLAIN_ROOT = ROOT / "benchmarks/TPCH/out/explain"
DEFAULT_MANIFEST = ROOT / "benchmarks/TPCH/pipeline/manifest.json"
DEFAULT_OUTPUT = DEFAULT_EXPLAIN_ROOT / "summary.csv"

SF_DB_RE = re.compile(r"tpch_sf(\d{3})\.duckdb$", re.IGNORECASE)

CSV_COLUMNS = [
    "query",
    "explainable",
    "has_results",
    "scale_factor",
    "db_basename",
    "db_path",
    "warmup",
    "runs",
    "cold",
    "unoptimized",
    "timestamp",
    "git_sha",
    "plans_match",
    "ref_min_s",
    "ref_median_s",
    "ref_max_s",
    "ref_p95_s",
    "gen_min_s",
    "gen_median_s",
    "gen_max_s",
    "gen_p95_s",
    "gen_setup_ms",
    "gen_over_ref_median",
]


def scale_factor_from_db_path(db_path: str) -> str:
    """Infer TPC-H scale factor from tpch_sfNNN.duckdb filename (NNN = sf * 100)."""
    name = pathlib.Path(db_path).name
    m = SF_DB_RE.search(name)
    if not m:
        return ""
    return f"{int(m.group(1)) / 100:g}"


def load_explainable_queries(manifest_path: pathlib.Path) -> dict[int, bool]:
    if not manifest_path.is_file():
        return {}
    data = json.loads(manifest_path.read_text())
    out: dict[int, bool] = {}
    for key, entry in data.get("queries", {}).items():
        try:
            q = int(key)
        except ValueError:
            continue
        out[q] = entry.get("translate") == "ok"
    return out


def load_summary(path: pathlib.Path) -> dict[str, Any] | None:
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as e:
        print(f"warn: skip {path}: {e}", file=sys.stderr)
        return None


def timing_field(timing: dict[str, Any] | None, key: str) -> str:
    if not timing:
        return ""
    val = timing.get(key)
    if val is None:
        return ""
    return f"{float(val):.6f}"


def row_from_summary(summary: dict[str, Any], *, explainable: bool) -> dict[str, str]:
    ref_timing = (summary.get("arms") or {}).get("ref", {}).get("timing")
    gen_arm = (summary.get("arms") or {}).get("gen", {})
    gen_timing = gen_arm.get("timing")

    db_path = str(summary.get("db_path") or "")
    ref_median = float(ref_timing["median"]) if ref_timing and "median" in ref_timing else None
    gen_median = float(gen_timing["median"]) if gen_timing and "median" in gen_timing else None
    ratio = ""
    if ref_median and ref_median > 0 and gen_median is not None:
        ratio = f"{gen_median / ref_median:.4f}"

    plan_cmp = summary.get("plan_comparison") or {}
    setup_ms = gen_arm.get("setup_ms")

    return {
        "query": str(summary.get("query", "")),
        "explainable": "yes" if explainable else "no",
        "has_results": "yes",
        "scale_factor": scale_factor_from_db_path(db_path),
        "db_basename": pathlib.Path(db_path).name if db_path else "",
        "db_path": db_path,
        "warmup": str(summary.get("warmup", "")),
        "runs": str(summary.get("runs", "")),
        "cold": "yes" if summary.get("cold") else "no",
        "unoptimized": "yes" if summary.get("unoptimized") else "no",
        "timestamp": str(summary.get("timestamp", "")),
        "git_sha": str(summary.get("git_sha") or ""),
        "plans_match": "yes" if plan_cmp.get("plans_match") else "no",
        "ref_min_s": timing_field(ref_timing, "min"),
        "ref_median_s": timing_field(ref_timing, "median"),
        "ref_max_s": timing_field(ref_timing, "max"),
        "ref_p95_s": timing_field(ref_timing, "p95"),
        "gen_min_s": timing_field(gen_timing, "min"),
        "gen_median_s": timing_field(gen_timing, "median"),
        "gen_max_s": timing_field(gen_timing, "max"),
        "gen_p95_s": timing_field(gen_timing, "p95"),
        "gen_setup_ms": f"{float(setup_ms):.3f}" if setup_ms is not None else "",
        "gen_over_ref_median": ratio,
    }


def empty_row(query: int, *, explainable: bool) -> dict[str, str]:
    return {col: "" for col in CSV_COLUMNS} | {
        "query": str(query),
        "explainable": "yes" if explainable else "no",
        "has_results": "no",
    }


def collect_rows(
    explain_root: pathlib.Path,
    manifest_path: pathlib.Path,
    *,
    include_non_explainable: bool,
    include_pending: bool,
) -> list[dict[str, str]]:
    explainable = load_explainable_queries(manifest_path)
    results_by_query: dict[int, dict[str, str]] = {}

    if explain_root.is_dir():
        for summary_path in sorted(explain_root.glob("q*/summary.json")):
            summary = load_summary(summary_path)
            if not summary:
                continue
            q = int(summary.get("query", 0))
            if q <= 0:
                continue
            is_explainable = explainable.get(q, False)
            if not is_explainable and not include_non_explainable:
                continue
            results_by_query[q] = row_from_summary(summary, explainable=is_explainable)

    rows: list[dict[str, str]] = []
    seen: set[int] = set()
    for q in sorted(explainable.keys()):
        if not explainable[q]:
            continue
        seen.add(q)
        if q in results_by_query:
            rows.append(results_by_query[q])
        elif include_pending:
            rows.append(empty_row(q, explainable=True))

    if include_non_explainable:
        for q in sorted(results_by_query.keys()):
            if q not in seen:
                rows.append(results_by_query[q])

    return rows


def write_csv(rows: list[dict[str, str]], out_path: pathlib.Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_COLUMNS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--explain-root",
        type=pathlib.Path,
        default=DEFAULT_EXPLAIN_ROOT,
        help=f"Directory with qN/summary.json (default: {DEFAULT_EXPLAIN_ROOT.relative_to(ROOT)})",
    )
    ap.add_argument(
        "--manifest",
        type=pathlib.Path,
        default=DEFAULT_MANIFEST,
        help="Pipeline manifest for explainable (translate: ok) queries",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=pathlib.Path,
        default=DEFAULT_OUTPUT,
        help=f"Output CSV path (default: {DEFAULT_OUTPUT.relative_to(ROOT)})",
    )
    ap.add_argument(
        "--include-non-explainable",
        action="store_true",
        help="Include q*/summary.json even when manifest translate != ok",
    )
    ap.add_argument(
        "--no-pending",
        action="store_true",
        help="Omit explainable queries that have no summary.json yet",
    )
    args = ap.parse_args()

    rows = collect_rows(
        args.explain_root.resolve(),
        args.manifest.resolve(),
        include_non_explainable=args.include_non_explainable,
        include_pending=not args.no_pending,
    )
    if not rows:
        print("No rows to write (run tpch:explain-bench first?)", file=sys.stderr)
        return 1

    out_path = args.output.resolve()
    write_csv(rows, out_path)

    with_results = sum(1 for r in rows if r["has_results"] == "yes")
    explainable = sum(1 for r in rows if r["explainable"] == "yes")
    print(
        f"Wrote {len(rows)} rows ({with_results} with results, {explainable} explainable) -> {out_path}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

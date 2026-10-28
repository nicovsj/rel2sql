#!/usr/bin/env python3
"""Compare the results of translated TPC-H SQL with the reference SQL, query by query.

For each query, runs benchmarks/TPCH/sql/q<N>.sql and the translated script <gen-sql-dir>/q<N>.sql
(which defines the view `result`) on a copy of the database, and compares the two results:

  * as multisets of rows, ignoring column names and row order (Rel relations are sets, and the
    reference's ORDER BY only orders);
  * when the translation has more columns than the reference, its leading columns (keys or sort
    indices the reference does not output) are dropped, and the number dropped is reported;
  * reference rows whose values are all NULL are ignored: Rel has no NULL, and aggregating the empty
    relation gives the empty relation, where SQL returns one row of NULLs;
  * numbers are equal up to a relative tolerance of 1e-6.

Usage:
  scripts/tpch_compare_results.py --gen-sql-dir benchmarks/TPCH/out/wide-sql \
      --db benchmarks/TPCH/data/tpch_sf001.duckdb            # all 22
  scripts/tpch_compare_results.py --gen-sql-dir ... --db ... 6 19
"""
from __future__ import annotations

import argparse
import math
import pathlib
import shutil
import sys
import tempfile
from collections import Counter

import duckdb

ROOT = pathlib.Path(__file__).resolve().parent.parent
REF_SQL_DIR = ROOT / "benchmarks/TPCH/sql"
REL_TOL = 1e-6


def normalize(value):
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    try:
        return float(value) if not isinstance(value, str) else value
    except (TypeError, ValueError):
        return str(value)


def rows_equal(a: tuple, b: tuple) -> bool:
    for x, y in zip(a, b):
        if isinstance(x, float) and isinstance(y, float):
            if not math.isclose(x, y, rel_tol=REL_TOL, abs_tol=REL_TOL):
                return False
        elif x != y:
            return False
    return True


def multiset_equal(ref: list[tuple], gen: list[tuple]) -> bool:
    if len(ref) != len(gen):
        return False
    # Exact matches first, then pair the rest up to the numeric tolerance.
    remaining = Counter(gen)
    unmatched = []
    for row in ref:
        if remaining[row] > 0:
            remaining[row] -= 1
        else:
            unmatched.append(row)
    leftover = list(remaining.elements())
    for row in unmatched:
        for i, cand in enumerate(leftover):
            if rows_equal(row, cand):
                leftover.pop(i)
                break
        else:
            return False
    return True


def compare(query: int, gen_sql_dir: pathlib.Path, db: pathlib.Path) -> tuple[bool, str]:
    gen_path = gen_sql_dir / f"q{query}.sql"
    if not gen_path.exists():
        return False, f"missing {gen_path}"
    con = duckdb.connect(str(db))
    try:
        ref = con.execute((REF_SQL_DIR / f"q{query}.sql").read_text()).fetchall()
        con.execute(gen_path.read_text())
        gen = con.execute("SELECT * FROM result").fetchall()
    except Exception as exc:  # noqa: BLE001 -- report and go on with the next query
        return False, f"error: {str(exc).splitlines()[0][:200]}"
    finally:
        con.close()

    ref = [tuple(normalize(v) for v in row) for row in ref if any(v is not None for v in row)]
    gen = [tuple(normalize(v) for v in row) for row in gen]
    ref_width = len(ref[0]) if ref else None
    gen_width = len(gen[0]) if gen else None
    dropped = 0
    if ref_width is not None and gen_width is not None and gen_width > ref_width:
        dropped = gen_width - ref_width
        gen = [row[dropped:] for row in gen]
    ok = multiset_equal(ref, gen)
    note = f"{len(ref)} rows" + (f", {dropped} leading column(s) dropped" if dropped else "")
    if not ok:
        note += f"; translation has {len(gen)} rows"
    return ok, note


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("queries", nargs="*", type=int)
    ap.add_argument("--gen-sql-dir", type=pathlib.Path, required=True)
    ap.add_argument("--db", type=pathlib.Path, required=True)
    args = ap.parse_args()
    queries = args.queries or list(range(1, 23))

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        # The translated scripts create views: work on a copy, so the database stays as built.
        db_copy = pathlib.Path(tmp) / args.db.name
        shutil.copy(args.db, db_copy)
        for q in queries:
            ok, note = compare(q, args.gen_sql_dir, db_copy)
            failures += not ok
            print(f"  [{'OK' if ok else 'DIFF'}] Q{q}: {note}")
    print(f"\n{len(queries) - failures}/{len(queries)} queries return the reference's values")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Validate benchmarks/TPCH/rel/ai-rewrites/<q>.rel against the reference SQL.

For each query: rewrite -> translate -> run `SELECT * FROM result` on a throwaway
copy of the comparison DB -> compare against benchmarks/TPCH/sql/q<n>.sql.

The comparison mirrors RunCompare in tpch_pipeline_lib.cc: order-insensitive,
column names ignored, 1e-6 absolute tolerance on numbers.

With --official the source is benchmarks/TPCH/rel/official instead, and the
rewrite step is skipped: those files are fed to rel2sql exactly as they sit on
disk, which is the property that directory exists to guarantee.

Usage:
  scripts/ai_rewrite_check.py            # all queries present in ai-rewrites/
  scripts/ai_rewrite_check.py 6 19       # just these
  scripts/ai_rewrite_check.py --sql 6    # also dump the generated SQL
  scripts/ai_rewrite_check.py --official # official/, straight into rel2sql
"""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
AI_DIR = ROOT / "benchmarks/TPCH/rel/ai-rewrites"
OFFICIAL_DIR = ROOT / "benchmarks/TPCH/rel/official"
REF_SQL = ROOT / "benchmarks/TPCH/sql"
EDB = ROOT / "benchmarks/TPCH/rel/tpch_edb.edb"
BIN = ROOT / "bazel-bin/rel2sql_bin"
DEFAULT_DB = ROOT / "benchmarks/TPCH/data/tpch_sf001.duckdb"

TIMEOUT = 120


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT, **kw)


def parse_rows(out: str) -> list[list[str]]:
    rows = []
    for line in out.splitlines():
        if not line.strip():
            continue
        rows.append(line.split("|"))
    return rows


def norm(cell: str):
    """Numbers compare numerically; everything else as a trimmed string."""
    s = cell.strip()
    try:
        return ("n", float(s))
    except ValueError:
        return ("s", s)


def rows_equal(a: list[list[str]], b: list[list[str]], tol=1e-6) -> str | None:
    if len(a) != len(b):
        return f"row count {len(a)} != {len(b)}"
    if a and len(a[0]) != len(b[0]):
        return f"column count {len(a[0])} != {len(b[0])}"
    ka = sorted([norm(c) for c in r] for r in a)
    kb = sorted([norm(c) for c in r] for r in b)
    for ra, rb in zip(ka, kb):
        for ca, cb in zip(ra, rb):
            if ca[0] != cb[0]:
                return f"type mismatch {ca!r} vs {cb!r}"
            if ca[0] == "n":
                if abs(ca[1] - cb[1]) > tol:
                    return f"value {ca[1]} != {cb[1]}"
            elif ca[1] != cb[1]:
                return f"value {ca[1]!r} != {cb[1]!r}"
    return None


def duckdb(db: pathlib.Path, script: str):
    return run(["duckdb", str(db), "-noheader", "-list", "-init", "/dev/null"], input=script)


def check(q: int, db: pathlib.Path, dump_sql: bool, official: bool = False) -> tuple[bool, str, int]:
    if official:
        # The point of official/ is that nothing runs before rel2sql, so this path
        # deliberately skips tpch_rewrite.py and feeds the file to the CLI as-is.
        rel_src = OFFICIAL_DIR / f"{q}.rel"
        if not rel_src.exists():
            return False, "no official file", 0
        rel_text = rel_src.read_text()
    else:
        rel_src = AI_DIR / f"{q}.rel"
        if not rel_src.exists():
            return False, "no ai-rewrite file", 0
        rw = run([sys.executable, str(ROOT / "scripts/tpch_rewrite.py"), str(q), "--queries-dir", str(AI_DIR)])
        if rw.returncode != 0:
            return False, "rewrite failed: " + rw.stderr.strip()[:200], 0
        rel_text = rw.stdout

    with tempfile.TemporaryDirectory() as td:
        tdp = pathlib.Path(td)
        relf = tdp / f"q{q}.rel"
        relf.write_text(rel_text)
        tr = run([str(BIN), "-e", str(EDB), "-f", str(relf)])
        if tr.returncode != 0:
            msg = (tr.stderr or tr.stdout).strip().splitlines()
            return False, "TRANSLATE: " + (msg[0][:160] if msg else "?"), 0
        sql = tr.stdout
        nsrc = len(re.findall(r"(?:FROM|,)\s+[a-z_][a-z0-9_]*\s+AS\s+T\d+", sql))
        if dump_sql:
            print(sql)

        work = tdp / "work.duckdb"
        shutil.copyfile(db, work)
        try:
            gen = duckdb(work, sql + "\nSELECT * FROM result;\n")
        except subprocess.TimeoutExpired:
            return False, f"TIMEOUT (>{TIMEOUT}s)", nsrc
        if gen.returncode != 0 or "Error" in gen.stderr:
            return False, "RUN: " + (gen.stderr.strip().splitlines() or ["?"])[0][:160], nsrc

        ref = duckdb(db, REF_SQL.joinpath(f"q{q}.sql").read_text())
        if ref.returncode != 0:
            return False, "REF: " + ref.stderr.strip()[:160], nsrc

        diff = rows_equal(parse_rows(ref.stdout), parse_rows(gen.stdout))
        if diff:
            return False, f"MISMATCH: {diff}", nsrc
        return True, f"{len(parse_rows(gen.stdout))} rows", nsrc


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("queries", nargs="*", type=int)
    ap.add_argument("--db", type=pathlib.Path, default=DEFAULT_DB)
    ap.add_argument("--sql", action="store_true", help="print generated SQL")
    ap.add_argument("--official", action="store_true",
                    help="check benchmarks/TPCH/rel/official, fed to rel2sql with no rewrite step")
    a = ap.parse_args(argv)

    src_dir = OFFICIAL_DIR if a.official else AI_DIR
    qs = a.queries or sorted(int(p.stem) for p in src_dir.glob("*.rel"))
    if not qs:
        print("no queries in", src_dir)
        return 1

    ok = 0
    for q in qs:
        passed, msg, nsrc = check(q, a.db, a.sql, official=a.official)
        print(f"Q{q:<3} {'PASS' if passed else 'FAIL':4}  sources={nsrc:<3}  {msg}")
        ok += passed
    print(f"\n{ok}/{len(qs)} pass")
    return 0 if ok == len(qs) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

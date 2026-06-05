#!/usr/bin/env python3
"""Build a DuckDB file with TPC-H data for rel2sql + reference SQL.

Uses DuckDB's built-in TPC-H extension (no separate dbgen install):
  INSTALL tpch; LOAD tpch; CALL dbgen(sf=...);

Creates:
  - Wide reference tables: customer_csv, orders_rel, lineitem_rel, ...
  - rel2sql EDB projections: c_name, l_quantity, o_custkey, ... (A1/A2/A3)

Default output path follows scale factor, e.g. SF 0.01 -> tpch_sf001.duckdb, SF 1.0 -> tpch_sf100.duckdb.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MAPPING = ROOT / "benchmarks/TPCH/rel/tpch_schema_mapping.rel"
DATA_DIR = ROOT / "benchmarks/TPCH/data"


def sf_to_filename(sf: float) -> str:
    """Map scale factor to tpch_sfNNN.duckdb (sf * 100, zero-padded to 3 digits)."""
    code = int(round(sf * 100))
    return f"tpch_sf{code:03d}.duckdb"


def default_db_path(sf: float) -> pathlib.Path:
    return DATA_DIR / sf_to_filename(sf)

FUNC_RE = re.compile(r"^@function\s+def\s+(\w+)\s*\{\s*\w+_rel\[:(\w+)\]\s*\}")
DEF_RE = re.compile(r"^def\s+(\w+)\s*\(")

# dbgen table rename targets (avoid clashing with rel2sql EDB table names).
TPCH_BASE = {
    "customer": "tpch_customer",
    "orders": "tpch_orders",
    "lineitem": "tpch_lineitem",
    "nation": "tpch_nation",
    "region": "tpch_region",
    "part": "tpch_part",
    "supplier": "tpch_supplier",
    "partsupp": "tpch_partsupp",
}

# Prefix on attribute -> base table (after rename).
PREFIX_TABLE = [
    ("ps_", "tpch_partsupp"),
    ("l_", "tpch_lineitem"),
    ("c_", "tpch_customer"),
    ("o_", "tpch_orders"),
    ("p_", "tpch_part"),
    ("s_", "tpch_supplier"),
    ("n_", "tpch_nation"),
    ("r_", "tpch_region"),
]

REFERENCE_VIEWS = [
    ("customer_csv", "tpch_customer"),
    ("orders_rel", "tpch_orders"),
    ("lineitem_rel", "tpch_lineitem"),
    ("nation_csv", "tpch_nation"),
    ("region_csv", "tpch_region"),
    ("part_csv", "tpch_part"),
    ("supplier_csv", "tpch_supplier"),
    ("partsupp_csv", "tpch_partsupp"),
]

UNARY_ENTITY = {
    "customer": ("tpch_customer", "c_custkey"),
    "nation": ("tpch_nation", "n_nationkey"),
    "orders": ("tpch_orders", "o_orderkey"),
    "part": ("tpch_part", "p_partkey"),
    "region": ("tpch_region", "r_regionkey"),
    "supplier": ("tpch_supplier", "s_suppkey"),
}

BINARY_ENTITY = {
    "lineitem": ("tpch_lineitem", "l_orderkey", "l_linenumber"),
    "partsupp": ("tpch_partsupp", "ps_partkey", "ps_suppkey"),
}


def table_for_attr(attr: str) -> str:
    for prefix, table in PREFIX_TABLE:
        if attr.startswith(prefix):
            return table
    raise ValueError(f"unknown attribute prefix: {attr}")


def parse_projections() -> tuple[list[tuple[str, str, str]], list[tuple[str, str, str, str]]]:
    """Return (binary_projections, ternary_projections) as (rel_name, table, col_a1, ...)."""
    binary: list[tuple[str, str, str]] = []
    ternary: list[tuple[str, str, str, str]] = []
    for line in MAPPING.read_text().splitlines():
        m = FUNC_RE.match(line.strip())
        if not m:
            continue
        rel, attr = m.group(1), m.group(2)
        table = table_for_attr(attr)
        if rel.startswith("l_") or rel.startswith("ps_"):
            if rel.startswith("l_"):
                ternary.append((rel, table, "l_orderkey", "l_linenumber", attr))
            else:
                ternary.append((rel, table, "ps_partkey", "ps_suppkey", attr))
        else:
            # Entity key is first column of *_rel accessor.
            key = {
                "tpch_customer": "c_custkey",
                "tpch_orders": "o_orderkey",
                "tpch_nation": "n_nationkey",
                "tpch_region": "r_regionkey",
                "tpch_part": "p_partkey",
                "tpch_supplier": "s_suppkey",
            }[table]
            binary.append((rel, table, key, attr))
    return binary, ternary


def sql_ident(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def build_sql(sf: float, db_path: pathlib.Path) -> str:
    binary, ternary = parse_projections()
    lines = [
        "INSTALL tpch;",
        "LOAD tpch;",
        f"CALL dbgen(sf := {sf});",
        "",
        "-- Keep dbgen tables under tpch_* names.",
    ]
    for src, dst in TPCH_BASE.items():
        lines.append(f"ALTER TABLE {sql_ident(src)} RENAME TO {sql_ident(dst)};")

    lines.append("")
    lines.append("-- Reference SQL (wide TPC-H column names).")
    for view, base in REFERENCE_VIEWS:
        lines.append(f"CREATE OR REPLACE VIEW {sql_ident(view)} AS SELECT * FROM {sql_ident(base)};")

    lines.append("")
    lines.append("-- rel2sql EDB unary entity relations.")
    for rel, (table, pk) in UNARY_ENTITY.items():
        lines.append(
            f"CREATE OR REPLACE TABLE {sql_ident(rel)} AS "
            f"SELECT {sql_ident(pk)} AS A1 FROM {sql_ident(table)};"
        )

    lines.append("")
    lines.append("-- rel2sql EDB binary entity keys.")
    for rel, (table, k1, k2) in BINARY_ENTITY.items():
        lines.append(
            f"CREATE OR REPLACE TABLE {sql_ident(rel)} AS "
            f"SELECT {sql_ident(k1)} AS A1, {sql_ident(k2)} AS A2 FROM {sql_ident(table)};"
        )

    lines.append("")
    lines.append("-- rel2sql EDB column projections (binary).")
    for rel, table, k, attr in binary:
        lines.append(
            f"CREATE OR REPLACE TABLE {sql_ident(rel)} AS "
            f"SELECT {sql_ident(k)} AS A1, {sql_ident(attr)} AS A2 FROM {sql_ident(table)};"
        )

    lines.append("")
    lines.append("-- rel2sql EDB column projections (ternary).")
    for rel, table, k1, k2, attr in ternary:
        lines.append(
            f"CREATE OR REPLACE TABLE {sql_ident(rel)} AS "
            f"SELECT {sql_ident(k1)} AS A1, {sql_ident(k2)} AS A2, {sql_ident(attr)} AS A3 "
            f"FROM {sql_ident(table)};"
        )

    lines.append("")
    lines.append("-- Statistics for the optimizer (important at SF 1+).")
    for table in TPCH_BASE.values():
        lines.append(f"ANALYZE {sql_ident(table)};")

    lines.append("")
    lines.append("-- Sanity row counts")
    lines.append("SELECT 'tpch_lineitem' AS rel, COUNT(*)::BIGINT AS n FROM tpch_lineitem;")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--sf",
        type=float,
        default=0.01,
        help="TPC-H scale factor (default 0.01)",
    )
    ap.add_argument(
        "--output",
        type=pathlib.Path,
        default=None,
        help="DuckDB file path (default: benchmarks/TPCH/data/tpch_sfNNN.duckdb from --sf)",
    )
    ap.add_argument(
        "--duckdb",
        default="duckdb",
        help="duckdb CLI executable",
    )
    args = ap.parse_args()

    out = (args.output if args.output is not None else default_db_path(args.sf)).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    sql = build_sql(args.sf, out)
    tmp_sql = out.parent / f".build_{out.stem}.sql"
    tmp_sql.write_text(sql)

    print(f"Generating TPC-H SF={args.sf} -> {out}", file=sys.stderr)
    proc = subprocess.run(
        [args.duckdb, str(out)],
        input=sql,
        capture_output=True,
        text=True,
    )
    tmp_sql.unlink(missing_ok=True)
    if proc.returncode != 0:
        print(proc.stdout, file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        return proc.returncode

    if proc.stdout.strip():
        print(proc.stdout.strip(), file=sys.stderr)
    print(f"Wrote {out} ({out.stat().st_size // 1024} KiB)", file=sys.stderr)
    print(f"  export TPCH_DUCKDB_PATH={out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

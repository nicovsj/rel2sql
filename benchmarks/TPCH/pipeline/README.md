# TPC-H translation pipeline tests

End-to-end benchmark harness for the Rel → SQL path used by TPC-H workloads.

## Stages (per query, driven by [`manifest.json`](manifest.json))

| Stage | What it checks |
|-------|----------------|
| **rewrite** | [`scripts/tpch_rewrite.py`](../../../scripts/tpch_rewrite.py) strips annotations, substitutes `@@N`, rewrites field access |
| **translate** | [`rel2sql_bin`](../../../BUILD) with [`tpch_edb.edb`](../rel/tpch_edb.edb) |
| **execute_empty** | Translated SQL runs on in-memory DuckDB with empty EDB tables ([`tpch_edb_duckdb_types.edb`](../rel/tpch_edb_duckdb_types.edb): `VARCHAR` on string value columns) |
| **compare_local** | Optional: same result rows as [`benchmarks/TPCH/sql/qN.sql`](../sql/) (local only) |

Update expectations in `manifest.json` after fixing translation, then regenerate headers:

```sh
python3 scripts/gen_tpch_manifest_cc.py benchmarks/TPCH/pipeline/manifest.json benchmarks/TPCH/pipeline/manifest_data.h
```

## CI vs local

| Command | CI (no TPC-H data) | Local (with data) |
|---------|-------------------|-------------------|
| `task tpch:pipeline-test` | Runs all manifest queries through stages 1–3 | Same |
| `task tpch:full-db-run -- 18` | Pipeline only | Add `TPCH_DUCKDB_PATH` + `--compare` for result diff |
| `task tpch:emit-sql` | Emit SQL files only | Same |

**CI** runs `bazel test //benchmarks/TPCH:tpch_pipeline_test` as part of `bazel test //...`.

**Local compare** requires `TPCH_DUCKDB_PATH` pointing at a DuckDB database that already contains:

- Tables used by reference SQL (`lineitem_rel`, `customer_csv`, …)
- Tables/views used by rel2sql output (`l_quantity`, `o_custkey`, …, plus `result` after running the translated script)

Generate a small local database (SF **0.01**, DuckDB `tpch` extension — no separate dbgen):

```sh
task tpch:build-db
export TPCH_DUCKDB_PATH="$(pwd)/benchmarks/TPCH/data/tpch_sf001.duckdb"

# Emit SQL, load views, print answer rows
task tpch:run-query -- 18

# Compare translated vs reference SQL on the same DB
task tpch:full-db-run -- --compare 18
```

See [`../data/README.md`](../data/README.md). Schema names differ between reference SQL and rel2sql output; see [`docs/tpch-rel2sql-roundtrip-catalog.md`](../../../docs/tpch-rel2sql-roundtrip-catalog.md).

```sh
# Pipeline only (no data)
task tpch:full-db-run -- 18

# All manifest queries
task tpch:full-db-run -- --compare --all
```

## Environment variables

Copy [`.env.example`](../../../.env.example) to `.env` at the repo root; `task` loads it for all tasks (`dotenv` in `Taskfile.yml`).

| Variable | Purpose |
|----------|---------|
| `REL2SQL_REPO_ROOT` | Repo root (set automatically by `tpch_full_db_run.sh`) |
| `REL2SQL_BIN` | Path to `rel2sql_bin` (default `bazel-bin/rel2sql_bin`) |
| `TPCH_DUCKDB_PATH` | DuckDB file for `--compare` in `tpch_runner` |

## Reference SQL vs rel2sql SQL

- **Text diff:** `diff -u benchmarks/TPCH/sql/q18.sql benchmarks/TPCH/out/sql/q18.sql` after `task tpch:emit-sql`.
- **Result diff:** `tpch_runner --compare` (multiset, float tolerance ≈ 1.0).

Empty-table execution in CI only proves the script is syntactically valid on DuckDB, not semantic equivalence with TPC-H.

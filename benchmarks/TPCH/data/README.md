# TPC-H DuckDB data (local)

This directory holds **generated** DuckDB databases for running translated TPC-H SQL on real data.

## Scale factors and file names

| SF | File | Rough size (order of magnitude) |
|----|------|----------------------------------|
| 0.01 | `tpch_sf001.duckdb` | Few MB; fast smoke tests |
| 0.1 | `tpch_sf010.duckdb` | Tens of MB |
| 1.0 | `tpch_sf100.duckdb` | ~1GB+; use for EXPLAIN ANALYZE experiments |

Requires [DuckDB](https://duckdb.org/) on your `PATH` (built-in `tpch` extension — no separate dbgen).

```sh
# Quick smoke (default SF 0.01)
task tpch:build-db

# EXPLAIN bench default (SF 1.0; may take several minutes)
task tpch:build-db:sf1
# or
python3 scripts/tpch_build_duckdb.py --sf 1.0
python3 scripts/tpch_build_duckdb.py --sf 0.1   # -> tpch_sf010.duckdb
```

Each database includes:

- **Reference SQL** views: `customer_csv`, `orders_rel`, `lineitem_rel`, …
- **rel2sql EDB** tables: `c_name`, `l_quantity`, `o_custkey`, … (`A1`/`A2`/`A3` columns)
- **`ANALYZE`** on `tpch_*` base tables after load

## Run queries

```sh
# Smoke on SF 0.01
export TPCH_DUCKDB_PATH="$(pwd)/benchmarks/TPCH/data/tpch_sf001.duckdb"
task tpch:run-query -- 18
task tpch:run-ref -- 18

# EXPLAIN bench on SF 1.0
export TPCH_DUCKDB_PATH="$(pwd)/benchmarks/TPCH/data/tpch_sf100.duckdb"
task tpch:emit-sql -- 18
task tpch:explain-bench -- 18
```

## Compare translated vs reference results

```sh
export TPCH_DUCKDB_PATH="$(pwd)/benchmarks/TPCH/data/tpch_sf001.duckdb"
task tpch:full-db-run -- --compare 18
```

Database files are gitignored; regenerate after clone.

## Environment variables

For `task tpch:*`, copy [`.env.example`](../../../.env.example) to `.env` at the repo root (gitignored). Task loads it automatically via `dotenv` in [`Taskfile.yml`](../../../Taskfile.yml).

```sh
cp .env.example .env
# edit TPCH_DUCKDB_PATH to your tpch_sf100.duckdb (absolute path)
task tpch:explain-bench -- 18
```

See [`../pipeline/README.md`](../pipeline/README.md) for the translation pipeline and EXPLAIN ANALYZE workflow.

# TPC-H over wide relations (Section 6.2 of the VLDB paper)

The 22 TPC-H queries written in Rel over one relation per TPC-H table, with one argument per column:
`w_lineitem(orderkey, partkey, suppkey, linenumber, ...)`, `w_orders(...)`, and so on.

| File | Contents |
|------|----------|
| `<N>.rel` | The Rel version of query N (parameters `@@k` are substituted by `scripts/tpch_rewrite.py`) |
| `tpch_wide_edb.edb` | Arities and keys of the wide relations, for `rel2sql_bin -e` |
| `wide_views.sql` | Views exposing the database's `tpch_*` tables as the wide relations (columns `A1`, `A2`, ...) |

`benchmarks/TPCH/results/interleaved-sf1.json` holds the raw times and the summary behind the paper's table.

## Reproducing the paper's results

Build the translator and the databases (DuckDB's TPC-H extension generates the data):

```sh
bazel build //:rel2sql_bin
python3 scripts/tpch_build_duckdb.py --sf 0.01      # benchmarks/TPCH/data/tpch_sf001.duckdb
python3 scripts/tpch_build_duckdb.py --sf 1         # benchmarks/TPCH/data/tpch_sf100.duckdb
```

Translate the queries, with and without the keys of the base relations:

```sh
scripts/tpch_wide_emit_sql.sh                       # -> benchmarks/TPCH/out/wide-sql
NOKEYS=1 scripts/tpch_wide_emit_sql.sh              # -> benchmarks/TPCH/out/wide-nokeys-sql
```

Check that the Rel versions compute the reference answers on the Rel engine (needs the RAICode
submodule; `task rel-engine:start` in another terminal), and that the translations do on DuckDB:

```sh
python3 scripts/tpch_rel_engine_check.py --db benchmarks/TPCH/data/tpch_sf001.duckdb
python3 scripts/tpch_compare_results.py --gen-sql-dir benchmarks/TPCH/out/wide-sql --db benchmarks/TPCH/data/tpch_sf001.duckdb
python3 scripts/tpch_compare_results.py --gen-sql-dir benchmarks/TPCH/out/wide-nokeys-sql --db benchmarks/TPCH/data/tpch_sf001.duckdb
```

Time the reference queries and both translations, interleaved, over five sessions. The paper uses
DuckDB 1.5.4: the script times with the Python package, so install that version
(`pip install duckdb==1.5.4`); the version is recorded in the output.

```sh
python3 scripts/tpch_interleaved_bench.py --db benchmarks/TPCH/data/tpch_sf100.duckdb \
    --arm nokeys=benchmarks/TPCH/out/wide-nokeys-sql --arm keys=benchmarks/TPCH/out/wide-sql \
    --sessions 5 --rounds 15 --out benchmarks/TPCH/results/interleaved-sf1.json
```

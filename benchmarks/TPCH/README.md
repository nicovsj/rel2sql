# TPC-H benchmark

Two questions about the Rel → SQL translator, asked over the 22 standard TPC-H queries:

1. **Is the translated SQL correct?** Does it return the same values as the reference
   TPC-H SQL, on real data?
2. **How much slower is it?** The reference SQL is the floor. The gap between it and
   rel2sql's output is the thing to minimise.

Question 1 is a hard gate (currently 22/22). Question 2 is open work.

## Why the translated SQL looks so different

The Rel EDB is **vertically decomposed**. Where the reference SQL reads one wide
`lineitem` table, Rel has a separate relation per attribute:

```
lineitem(orderkey, linenum)               -- the entity
l_shipdate(orderkey, linenum, date)       -- one relation per column
l_discount(orderkey, linenum, value)
l_quantity(orderkey, linenum, value)
```

So every attribute a query touches costs one table scan plus a join back on
`(orderkey, linenum)`. Reference Q6 is a single scan with four predicates; the
translation is at best four scans and three joins. That floor is inherent to the
schema, not a translator defect — but most of the *observed* gap is above the floor,
and that part is addressable. See [`rel/REWRITING.md`](rel/REWRITING.md).

## Layout

| Path | Contents |
|------|----------|
| [`sql/`](sql/) | Reference TPC-H SQL, `q1.sql` … `q22.sql`. **The source of truth.** |
| [`rel/queries/`](rel/queries/) | Original hand-written Rel queries |
| [`rel/ai-rewrites/`](rel/ai-rewrites/) | Rel queries rewritten from the reference SQL (see below) |
| [`rel/tpch_common_defs.rel`](rel/tpch_common_defs.rel) | Shared `@inline` helpers, prepended to queries that reference them |
| [`rel/tpch_edb.edb`](rel/tpch_edb.edb) | Base-relation arities handed to `rel2sql_bin -e` |
| [`rel/REWRITING.md`](rel/REWRITING.md) | **How to write Rel that translates to fast SQL**, with measurements |
| [`pipeline/`](pipeline/) | The correctness harness and its [manifest](pipeline/manifest.json) — see [pipeline/README.md](pipeline/README.md) |
| `out/` | Generated SQL (gitignored) — see [out/README.md](out/README.md) |
| `data/` | DuckDB databases (gitignored), `tpch_sf001.duckdb` and `tpch_sf100.duckdb` |
| `*.jl` | Upstream RelationalAI Julia harness, unrelated to rel2sql (section at the bottom) |

## The three arms

| Arm | SQL comes from | Meaning |
|-----|----------------|---------|
| **ref** | `sql/q<N>.sql` | Reference TPC-H SQL. The floor. |
| **current** | `out/sql/q<N>.sql` | Translation of `rel/queries/` |
| **ai** | `out/ai-sql/q<N>.sql` | Translation of `rel/ai-rewrites/` |

`rel/ai-rewrites/` is a second, independent set of Rel queries written directly from
the reference SQL rather than derived from `rel/queries/`. They exist to test how much
of the gap is the translator versus how the Rel was written. They are validated to
return the same values as the reference, and are **not** used by the pipeline tests.

## Running things

Build the databases first (both are gitignored and large):

```sh
task tpch:build-db          # SF 0.01 -> data/tpch_sf001.duckdb   (correctness)
task tpch:build-db:sf1      # SF 1    -> data/tpch_sf100.duckdb   (timing, ~650MB)
```

### Correctness

```sh
task tpch:emit-sql                                   # translate rel/queries -> out/sql
TPCH_DUCKDB_PATH=$PWD/benchmarks/TPCH/data/tpch_sf001.duckdb \
  task tpch:full-db-run -- --compare --all           # 22/22 value comparison
task tpch:ai:check                                   # same gate for rel/ai-rewrites
```

### Timing

```sh
task tpch:ai:bench3                  # ref vs current vs ai, with ratios  <- start here
task tpch:ai:bench3 -- 1 12 --runs 10
task tpch:explain-bench              # ref vs current, writes full plans to out/explain/
task tpch:ai:explain-bench           # ref vs ai,      writes full plans to out/explain-ai/
task tpch:explain-summary            # summarise out/explain/*/summary.json
```

**Time on SF1, not SF0.01.** At SF 0.01 the queries take milliseconds and the measured
ratio is dominated by process startup — the SF0.01 table once ranked Q2 as the worst
query in the suite at 436× when it is actually ~6×, and hid Q20, which really was
>1700×. `tpch:ai:bench3` reports DuckDB's own `EXPLAIN ANALYZE` latency rather than
wall clock for the same reason.

## Results

SF1, median of 5 runs, `EXPLAIN ANALYZE` latency in seconds (2026-09-17):

| Q | ref | current | ai | cur/ref | ai/ref |
|---|-----|---------|-----|---------|--------|
| Q1 | 0.0353 | 2.0546 | 2.2284 | 58.2× | 63.2× |
| Q2 | 0.0090 | 0.0399 | 0.0583 | 4.4× | 6.5× |
| Q3 | 0.0172 | 0.2785 | 0.0991 | 16.2× | 5.7× |
| Q4 | 0.0156 | 0.1347 | 0.5016 | 8.6× | 32.1× |
| Q5 | 0.0172 | 0.2570 | 0.1353 | 15.0× | 7.9× |
| Q6 | 0.0070 | 0.1048 | 0.0940 | 14.9× | 13.4× |
| Q7 | 0.0182 | 0.5487 | 0.2338 | 30.2× | 12.9× |
| Q8 | 0.0177 | 1.5005 | 0.1164 | 84.7× | 6.6× |
| Q9 | 0.0446 | 0.2973 | 0.3760 | 6.7× | 8.4× |
| Q10 | 0.0303 | 0.2280 | 0.0761 | 7.5× | 2.5× |
| Q11 | *empty* | *empty* | *empty* | – | – |
| Q12 | 0.0149 | 0.7796 | 0.3132 | 52.2× | 21.0× |
| Q13 | 0.0525 | 0.0825 | 0.1482 | 1.6× | 2.8× |
| Q14 | 0.0136 | 0.6761 | 0.0688 | 49.8× | 5.1× |
| Q15 | 0.0103 | 0.8492 | 0.1476 | 82.3× | 14.3× |
| Q16 | 0.0147 | 0.0289 | 0.0242 | 2.0× | 1.6× |
| Q17 | 0.0135 | 0.1303 | 0.1959 | 9.6× | 14.5× |
| Q18 | 0.0358 | 0.0639 | 0.0340 | 1.8× | 1.1× |
| Q19 | 0.0279 | 0.2806 | 0.0949 | 10.0× | 3.4× |
| Q20 | 0.0172 | 0.1872 | 0.1538 | 10.9× | 9.0× |
| Q21 | 0.0468 | *error* | *timeout* | – | – |
| Q22 | 0.0202 | 0.0377 | 0.0666 | 1.9× | 3.3× |
| **SUM** | **0.480** | **8.560** | **5.166** | **17.8×** | **10.8×** |

Read this carefully rather than by the bottom line:

- **Q1 is 43% of the `ai` total on its own.** Excluding it, `ai` is 2.94s against a
  0.44s reference, i.e. ~6.6× rather than 10.8×. Q1's eight aggregates each re-scan
  the grouping relation; Rel's tuple-of-aggregates form has no single
  `GROUP BY … 8 aggregates` equivalent, so this looks like a translator limitation.
- **The rewrites are not uniformly better**: 12 queries faster, 7 slower. Q4 is 3.7×
  *slower* than `current` despite having fewer sources (8 vs 11).
- **Source count does not predict time.** Several rewrites cut sources and got slower;
  one Q6 variant with 7 sources timed out where the 9-source version ran in 0.14s.
- **Q21 is broken in both arms.** `current` exhausts disk spilling to temp storage;
  `ai` times out at 120s. Reference is 0.047s.
- **Q11 is vacuous at SF1** — zero rows in every arm, so it is unmeasured, not fast.
  `PARAMS[11]` in [`scripts/tpch_rewrite.py`](../../scripts/tpch_rewrite.py) is `0.01`,
  the SF 0.01 threshold; the spec value is `0.0001/SF`. `sql/q11.sql` hardcodes the
  same `0.01`, so the two must change together.

## Gotchas worth knowing before you start

- **The value comparison is order-insensitive** ([`pipeline/tpch_pipeline_lib.cc`](pipeline/tpch_pipeline_lib.cc)),
  so a query only needs `sort`/`reverse_sort` where a `LIMIT` actually binds. At SF0.01
  that is only Q3 (10 rows) and Q10 (20). `rel/ai-rewrites/` omits the `LIMIT` on Q2,
  Q18 and Q21, where it does not bind at SF0.01 — those would diverge at larger scales.
- **Translating is not validating.** Several queries translated and ran for a long time
  while returning wrong answers. Always run the compare stage.
- **`--no-defs` ("solo") fails for any query using a shared helper.** That is expected,
  not a regression; see [out/README.md](out/README.md).
- Some negations are mistranslated, one union misaligns columns, and one expression form
  emits a duplicate table alias. These produce *wrong answers or errors, not slowness* —
  the specifics and their workarounds are in [`rel/REWRITING.md`](rel/REWRITING.md).

---

## Upstream RelationalAI harness (Julia)

The `*.jl` files in this directory are the original RelationalAI TPC-H harness. They
drive the Rel engine directly and are independent of rel2sql; nothing above uses them.

### Dependencies

A modified tpch-dbgen is needed, which is automatically downloaded when building
RelationalAI (found in `raicode/deps/usr/tpch-dbgen`).

To run the benchmark using RAI, you need a database connection.

```julia
    import RelationalAI
    dbname = "tpch"
    connection = RelationalAI.LocalConnection(dbname=Symbol(dbname); report=nothing)
```

This creates a connection to a local server, for executing the queries.
(See the RelationalAI docs for how to start a local server.)

To generate the data files to import into RelationalAI, if you have not yet done so,

```julia
    import RAI_Benchmarks.TPCH
    scale_factor = "0.01"
    conf = TPCH.rel_config(scale_factor, connection) # connection is actually not used in generate_data
    TPCH.generate_data(conf)
```

To do a full TPCH run (note: this will erase your previous TPCH DB as determined by
`conf.connection`):

```julia
    julia> TPCH.run_tpch(conf)
```

This will import the data files into RelationalAI (overwriting the `dbname` DB, if it
exists), and run the queries.

To just rebuild the RelationalAI database, but not run a full TPCH benchmark:

```julia
    julia> TPCH.init_db(conf)
```

The queries themselves are in `raicode/examples/tpch`.

If you want to run a TPCH query, e.g. query 5:

```julia
    julia> TPCH.run_query(conf, 5)
```

You can also generate a query, and then use it with `query`:

```julia
    julia> q = TPCH.generate_query(conf, 5)
    julia> query(connection, q; out=:result, debug = true)
```

To run a validation query:

```julia
    julia> TPCH.run_validation_query(conf, 5)
```

### End-to-end scripts

The following scripts may be helpful (note that you need a server up and running):

tpch-init.jl (initialize a database)

```julia
using RelationalAI
import RAI_Benchmarks.TPCH

conn = LocalConnection(dbname=:tpch)
conf = TPCH.rel_config("0.01", conn)
TPCH.generate_data(conf)
TPCH.init_db(conf)
```

tpch-query.jl (run a query using an existing database)

```julia
using RelationalAI
import RAI_Benchmarks.TPCH

conn = LocalConnection(dbname=:tpch)
conf = TPCH.rel_config("0.01", conn)
@time TPCH.run_query(conf, 1, conf.schema_name)
```

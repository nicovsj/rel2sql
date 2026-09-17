# Writing TPC-H Rel queries that translate to fast SQL

The Rel EDB is vertically decomposed: `l_shipdate(orderkey, linenum, date)`,
`l_discount`, `l_quantity` … are separate relations, where the reference SQL reads
one wide `lineitem`.  Every attribute a query touches therefore costs one table plus
a join back on `(orderkey, linenum)`.  That floor is inherent to the schema.

What is *not* inherent is the translated SQL re-deriving the same thing several
times.  The patterns below were each measured on SF1 (`tpch_sf100.duckdb`) against
the reference SQL, and each rewrite was checked to return byte-identical output.

## 1. Keep aggregates out of arithmetic (largest effect by far)

An arithmetic expression over an aggregate makes the translator re-derive the whole
aggregate once per operand position, and then join the copies **on the aggregate
value** rather than on the group key.

```rel
// BAD -- emits the same 7-source GROUP BY three times, joined on the sum
@inline def availqty_limit(pk, sk, sum_qty_limit):
    sum_qty_limit = 0.5 * sum[(o, l, qty): ...]

// GOOD -- aggregate materialised once, then scaled
def availqty_sum[pk, sk]: sum[(o, l, qty): ...]
@inline def availqty_limit[pk, sk]: 0.5 * availqty_sum[pk, sk]
```

Q20: 32 → 19 sources, **>121 s → 0.24 s** (reference 0.07 s).

The same rule applies to plain values: a relationally-bound variable that flows into
an arithmetic term gets re-derived *without key equalities*, producing a near
Cartesian product.  Inside arithmetic, always use the functional form that carries
the key arguments — `l_extendedprice[o, num] * l_discount[o, num]`, never
`l_extendedprice(o, num, ep) and ... and v = ep * disc`.

## 2. Use a set literal instead of OR over constants

A disjunction of equalities on a bound variable becomes a safety-domain CTE that
UNIONs the *whole* relation once per branch, unfiltered.

```rel
// BAD -- expands to a 17-way UNION of unfiltered l_shipmode scans
@inline def ship_mode(o, l, shipmode):
    l_shipmode(o, l, shipmode) and (shipmode = "AIR" or shipmode = "AIR REG")

// GOOD -- the idiom this file already uses for container_size
def air_modes { "AIR"; "AIR REG" }
@inline def ship_mode(o, l):
    exists((sm) | l_shipmode(o, l, sm) and air_modes(sm))
```

Q19: 55 → 34 sources, **0.92 s → 0.33 s** (reference 0.08 s).

## 3. Relational form for range predicates

A chained comparison desugars into two *independent* applications, so the relation is
scanned twice and joined only on the key.  Binding the value once collapses it.

```rel
// BAD -- two l_shipdate scans
lower_shipdate <= l_shipdate[o, num] < upper_shipdate

// GOOD -- one scan, both bounds as predicates on it
l_shipdate(o, num, sd) and lower_shipdate <= sd and sd < upper_shipdate
```

Q6: 11 → 9 sources, **0.21 s → 0.14 s** (reference 0.05 s).

Note this is only sound because these EDB relations are single-valued.  The *original*
form is what relies on that assumption without stating it: it admits `v1 >= lower`
and `v2 < upper` with `v1 != v2`.

## Things that look safe and are not

Check every rewrite by diffing output against the reference — inspection is not
enough.  All of these were tried and reverted:

- **Inside `sum[[keys]: value where cond]`** the relational form either fails to
  translate (`expression must be sourceable`) or silently returns wrong results.
  Q7's variant translated to *fewer* sources and produced a wrong answer.
- **Factoring a common conjunct out of two disjuncts** is not safe here.  Q13's
  `(customer(ck) and A) or (customer(ck) and B)` → `customer(ck) and (A or B)`
  returned 31 rows instead of 32.
- **Fewer sources does not mean faster.**  A Q6 variant with 7 sources (vs 9) timed
  out at SF1 because one source had no key equality.  A Q12 rewrite with the same
  source count was a 100× regression (0.87 s → 87 s).

## Reproducing the measurements

```sh
task tpch:emit-sql                                   # regenerate benchmarks/TPCH/out/sql
TPCH_DUCKDB_PATH=$PWD/benchmarks/TPCH/data/tpch_sf001.duckdb \
  task tpch:full-db-run -- --compare --all           # 22/22 correctness gate
TPCH_DUCKDB_PATH=$PWD/benchmarks/TPCH/data/tpch_sf100.duckdb \
  task tpch:explain-bench                            # SF1 timings
```

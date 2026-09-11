## Status as of 2026-09-11 (supersedes the stale claims below)

Everything below this section predates a session that got `rel2sql_bin` building and actually ran
all 22 queries through the pipeline. Treat the rest of this file as historical narrative, not
current status. Current status:

- Source of truth: [`benchmarks/TPCH/pipeline/manifest.json`](../benchmarks/TPCH/pipeline/manifest.json)
  (machine-checked by `bazel test //benchmarks/TPCH:tpch_pipeline_test`) and
  [`docs/tpch_rel2sql_plan.md`](tpch_rel2sql_plan.md) (source-grounded rewrite rationale, now
  verified against a real build — see its own file:line citations and the "verified" notes added
  during this session).
- **19 of 22 queries now translate** (`translate: ok`): 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 15,
  16, 17, 18, 19, 20, 21, 22. Only 8, 9, 14 still fail to translate.
- Of those 19, **8 also execute cleanly on empty tables** (`execute_empty: ok`): 1, 6, 10, 11, 12,
  17, 18, 20. The other 11 (2, 3, 4, 5, 7, 13, 15, 16, 19, 21, 22) translate without error but the
  generated SQL fails in DuckDB — these are **pre-existing SQL-codegen bugs in the translator**
  (dangling table aliases, `not(like_match(...))` inside an aggregate filter producing a
  multi-column `NOT IN` subquery, `MAX(...)` emitted directly in a `WHERE` clause, an
  OR-of-string-equality lowered to a `UNION` subquery with a mismatched column alias, and DuckDB
  type mismatches on date comparisons), **not** language gaps fixable by rewriting the `.rel`
  source. See each query's `execute_empty_note` in `manifest.json` for specifics.
- **The "EDB binding" explanation this file gives below for Q19/Q21 is wrong and stale.** Both
  queries translate and reach `execute_empty` today; Q19 additionally needed a rewrite for two
  *separate* gaps the original plan didn't anticipate: (a) `param <= l_quantity[o,l] <= param+10`
  where `param` is only ever compared (inlined with the substituted literal), and (b) `x = "SM
  CASE" or x = "SM BOX" or ...` — string-literal equality never grounds a variable in the current
  safety inferrer (only numeric/compositional equalities do), rewritten as an explicit fact table.
- Two genuinely new-to-this-session findings, not in the original plan, with **no query-level
  workaround found**: `date_year[R[k]]` used as a comparison operand (blocks Q8, Q9 — and Q9 was
  previously believed to already pass; `tests/test_translation.cc`'s own `TpchQ9*` tests fail at
  HEAD for the same reason), and division by a non-constant variable never grounds the dividing
  equality's target (`src/preprocessing/safety_inferrer.cc`'s `DivisorIsSafe`; blocks Q14 and,
  once date_year is fixed, Q8's `market_share`).
- `scripts/tpch_rewrite.py`'s `PARAMS` table had two latent bugs (an extra mid-list parameter for
  Q6 and Q20 that silently shifted every later `@@N` substitution to the wrong value) — fixed.
  Worth an audit pass on the rest of the table; see the plan doc.

---

# TPC-H Rel round-trip: semantics-preserving source rewrites (Section 1)

These edits bridge RAI Rel in `benchmarks/TPCH/rel/queries/` to the subset accepted by `RelParser.g4` **without** changing query meaning. Use them in a pre-pass before `GetSQLRel` (or extend the parser).

| ID | Rewrite | Rationale | Queries affected |
|----|---------|-----------|-------------------|
| 1a | Strip `@vectorized` and `@inline` | `T_AT_ID` is not in `RelParser.g4`; programs fail to parse. | Most queries; `tpch_common_defs.rel` |
| 1b | Replace `@@N` with concrete TPC-H parameter values | No `@@` syntax or binding in rel2sql. | All parameterized queries |
| 1c | Rename `mean[...]` to `average[...]` | Aggregate map uses `average` / `avg`. | Q1 |
| 1d | Replace `<++` (Q13) with explicit `union` of counted customers and `(customer, 0)` rows | `<++` is not in the grammar. | Q13 |
| 1e | Expand `__rest1...` (Q2) to an explicit result tuple | Variadic rest is not parsed. | Q2 |
| 1f | Replace `(x.y.z)` and `.date_year` with applications like `c_nationkey[o_custkey[o]]`, `date_year[o_orderdate[ok]]` | No field-access syntax in `expr`/`term`. | Q5, Q7, Q9, Q20, … |
| 1g | Keep `tpch_common_defs.rel` helpers; they become valid once 1a is done and builtins exist | `like_match`, `decimal` are builtins, not rewrite targets. | Q14, Q16, … |
| 1h | No change for embedded `/* :x :o ... */` SQL blocks | Lexer skips block comments. | All |
| 1i | If `:small` / `:medium` / `:large` defs fail to parse, split `container_size` into three plain defs | Tag-positional defs may need verification. | Q19 |
| 1j | *(Parser support, no rewrite script.)* Chained comparisons `a <= b <= c` parse as `a <= b and b <= c` | `RelParser.g4` `formula` rule; AST is `RelConjunction` of `RelComparison`s. | Q19 `quantity_range` |

---

# Per-query feasibility (Section 3)

After applying Section 1 rewrites, **core TPC-H builtins are now lowered in rel2sql** (`BuiltinResolver` + translator): aggregates including `count` / `mean`, `sort` / `reverse_sort` / `bottom` (with `ORDER BY` / `LIMIT` on `sql::ast::Select`), `parse_date` / `date_add` / `date_subtract` / `date_year`, `parse_decimal` / `decimal`, `default_value`, `substring`, typed literals (`^Day`, …), and parenthesized `like_match(...)`.

A smoke test of the **MVP subset (Q11, Q17, Q18, Q19, Q21)** through the rewriter (`scripts/tpch_rewrite.py`) + CLI originally surfaced a **second, independent class of blockers** that was not visible from reading the grammar alone — the **term/expr split**:

| Query | First grammar gap (resolved 2026-05) |
|-------|--------------------------------------|
| Q11 | `parse_decimal[64,4,"0.0001"] * sum[...]` — arithmetic between partial applications |
| Q17 | `parse_decimal[64,4,"0.2"] * average[...]` — same as Q11 |
| Q18 | `sum[l_quantity[ok]] > 300` — comparison whose LHS is a partial application |
| Q19 | `x = "SM CASE"` — comparison with a string literal RHS (only ID/numeric `term`s allowed) |
| Q21 | `l_suppkey[o1, l2] != o1l1_suppkey` — comparison whose LHS is a partial application |
| `tpch_common_defs.rel::l_revenue` | `l_extendedprice[o,num] * (1 - l_discount[o,num])` — blocks every query that imports the common defs |

The root cause was that `RelParser.g4` historically split the language into a **scalar `term` world** (IDs, numerical constants, `+ - * /`, parentheses) and a **relational `expr` world** (literals, products, applications, abstractions). TPC-H Rel programs freely mix them — they multiply two partial applications, compare a `sum[...]` against `300`, equate a variable with a string constant, etc. **Those forms now parse:**

* `term` accepts `appTerm` (partial applications such as `B[x]`), `strTerm`, `charTerm`, and `dateTerm` (string/char/date literals).
* `expr` orders `partialAppl` before `termExpr`, so the top-level `def output { sum[A] }` still binds to a `RelPartialApplication` (no behaviour change for the existing translation suite).
* A new `TermRewriter` pass lifts any `RelPartialApplication` that surfaces in a comparison/arithmetic operand into an existential conjoined with `{inner}(zᵢ)`, so downstream visitors (`VariablesVisitor`, `LiteralVisitor`, `TermPolynomialVisitor`, `SafetyInferrer`, `Translator`) keep seeing only `RelIDTerm` / `RelStringTerm` / numerical constants.
* See `tests/test_translation.cc::ComparisonStringLiteral`, `ComparisonPartialAppl`, `ComparisonNotEqualPartialAppl`, `OpTermPartialAppl`, `OpTermTwoPartialApps`, `RegressionTopLevelPartialApplStaysPartial`.

After this change the MVP subset gets past the term/expr boundary in `scripts/tpch_smoke.sh`. The remaining smoke-test failures are **unrelated to the term/expr split**:

| Query | Remaining blocker |
|-------|-------------------|
| Q11, Q17, Q18, Q19, Q21 | `ArityException` on base relations (`o_orderpriority`, `n_name`, `l_quantity`, `l_shipinstruct`, `l_suppkey`, …) — the CLI does not auto-register the TPC-H EDB schema; needs an explicit `RelationMap`. |

**Chained comparisons** (`a <= b <= c` as `a <= b and b <= c` with a shared middle `RelTerm`) are supported in `formula` since 2026-05; see `tests/test_translation.cc::ChainedComparison`. Q19’s `quantity_range` line parses once the binary is rebuilt.

### What works end-to-end today (translation suite)

`BuiltinResolver`, the new SQL AST nodes (`ORDER BY` / `LIMIT`, `Operation`, date/decimal/substring helpers, `LIKE`), and the safety / arity / variables visitors are all unit-tested in `tests/test_translation.cc` (see the new `Builtin*` cases — `BuiltinSortAsc`, `BuiltinReverseSort`, `BuiltinBottomFullApplication`, `BuiltinParseDate`, `BuiltinTypedLiteral{Day,Month,Year,FixedDecimal}`, `BuiltinDecimalNoValue`, `BuiltinParseDecimal`, `BuiltinDateAdd`, `BuiltinDateSubtract`, `BuiltinDateYear`, `BuiltinDefaultValue`, `BuiltinSubstring`, `BuiltinLikeMatchFormula`). The lowering pipeline produces SQL for every TPC-H builtin **once the surrounding Rel program parses**.

### What is needed to actually round-trip the MVP queries

1. ~~**Grammar: lift `partialAppl` and string/char literals into `term`**~~ **(resolved 2026-05)** — see the term/expr changes above. Arithmetic and comparisons now accept any expression that returns a single column.
2. **Grammar: optional sugar** `def f[x]: expr` / `def f(x): formula` (currently must be rewritten to `def f { [x]: expr }` by `scripts/tpch_rewrite.py`).
3. ~~**Grammar: chained comparison** `a <= b <= c`~~ **(resolved 2026-05)** — `formula` now accepts `head = term (comparator term)+`, lowered to a left-associated `RelConjunction` of `RelComparison` nodes with a shared middle term. Covered by `ChainedComparison` in `tests/test_translation.cc`.
4. **CLI: TPC-H EDB binding** — register base relations with **`-e benchmarks/TPCH/rel/tpch_edb.edb`** (generate via `python3 scripts/gen_tpch_edb.py`). The file lists `relation_name arity` per line; rel2sql still does not load CSV data, but arity analysis stops treating TPC-H atoms as undefined. Full round-trips may still fail later passes (e.g. safety) until the Rel program matches the supported subset.
5. **(Q13 only)** Implement the `<++` (left override) rewrite (1d) or add it to the grammar.
6. **(Q2 only)** Decide on a syntax for `__rest…` and implement (1e) — out of scope for the MVP demo.

### Tooling for replication

* `scripts/tpch_rewrite.py <N>` — apply Section 1 rewrites to a benchmark Rel file (concatenating `tpch_common_defs.rel`), substitute TPC-H parameters, cap `decimal[64, …]` / `parse_decimal[64, …]` to precision 32 for DuckDB, wrap shorthand defs in `{ … }`, rewrite `(a.b.c)` → `c[b[a]]`, and print the result.
* `scripts/gen_tpch_edb.py` — write `benchmarks/TPCH/rel/tpch_edb.edb` (`<relation> <arity>` lines) from `tpch_schema_mapping.rel` for use with `rel2sql_bin -e …`.
* `scripts/tpch_emit_sql.sh` — translate queries to `benchmarks/TPCH/out/sql/` (and rewritten Rel under `out/rel/`) for diff against `benchmarks/TPCH/sql/q{N}.sql`; `task tpch:emit-sql` or `task tpch:emit-sql -- 11`.
* `scripts/tpch_smoke.sh` — same pipeline for the MVP subset (Q11, Q17, Q18, Q19, Q21).

**Minimum viable subset (first round-trip demo):** Q11, Q17, Q18, Q19, Q21 — the term/expr split and chained comparisons are no longer blockers; the remaining gap for a CLI smoke run is an **EDB-binding strategy** for the base relations (plus optimizer caveats for some aggregate-heavy patterns).

**Full 22-query coverage** additionally needs end-to-end benchmark harnessing, Q13 `<++` rewrite, and verification of every verbatim SQL fragment on the target engine (DuckDB/Postgres/etc.).

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

---

# Per-query feasibility (Section 3)

After applying Section 1 rewrites, **core TPC-H builtins are now lowered in rel2sql** (`BuiltinResolver` + translator): aggregates including `count` / `mean`, `sort` / `reverse_sort` / `bottom` (with `ORDER BY` / `LIMIT` on `sql::ast::Select`), `parse_date` / `date_add` / `date_subtract` / `date_year`, `parse_decimal` / `decimal`, `default_value`, `substring`, typed literals (`^Day`, …), and parenthesized `like_match(...)`.

A smoke test of the **MVP subset (Q11, Q17, Q18, Q19, Q21)** through the rewriter (`scripts/tpch_rewrite.py`) + CLI shows a **second, independent class of blockers** that was not visible from reading the grammar alone:

| Query | First grammar gap that blocks parsing |
|-------|---------------------------------------|
| Q11 | `parse_decimal[64,4,"0.0001"] * sum[...]` — arithmetic between partial applications |
| Q17 | `parse_decimal[64,4,"0.2"] * average[...]` — same as Q11 |
| Q18 | `sum[l_quantity[ok]] > 300` — comparison whose LHS is a partial application |
| Q19 | `x = "SM CASE"` — comparison with a string literal RHS (only ID/numeric `term`s allowed) |
| Q21 | `l_suppkey[o1, l2] != o1l1_suppkey` — comparison whose LHS is a partial application |
| `tpch_common_defs.rel::l_revenue` | `l_extendedprice[o,num] * (1 - l_discount[o,num])` — blocks every query that imports the common defs |

The common root cause is that `RelParser.g4` splits the language into a **scalar `term` world** (IDs, numerical constants, `+ - * /`, parentheses) and a **relational `expr` world** (literals, products, applications, abstractions). TPC-H Rel programs freely mix them — they multiply two partial applications, compare a `sum[...]` against `300`, equate a variable with a string constant, etc. None of those forms parse today.

### What works end-to-end today (translation suite)

`BuiltinResolver`, the new SQL AST nodes (`ORDER BY` / `LIMIT`, `Operation`, date/decimal/substring helpers, `LIKE`), and the safety / arity / variables visitors are all unit-tested in `tests/test_translation.cc` (see the new `Builtin*` cases — `BuiltinSortAsc`, `BuiltinReverseSort`, `BuiltinBottomFullApplication`, `BuiltinParseDate`, `BuiltinTypedLiteral{Day,Month,Year,FixedDecimal}`, `BuiltinDecimalNoValue`, `BuiltinParseDecimal`, `BuiltinDateAdd`, `BuiltinDateSubtract`, `BuiltinDateYear`, `BuiltinDefaultValue`, `BuiltinSubstring`, `BuiltinLikeMatchFormula`). The lowering pipeline produces SQL for every TPC-H builtin **once the surrounding Rel program parses**.

### What is needed to actually round-trip the MVP queries

1. **Grammar: lift `partialAppl` and string/char literals into `term`** (or introduce a `scalar` non-terminal that subsumes both), so that arithmetic and comparisons can use any expression that returns a single column. The `RelASTBuilder` and downstream visitors will need matching updates per `AGENTS.md`.
2. **Grammar: optional sugar** `def f[x]: expr` / `def f(x): formula` (currently must be rewritten to `def f { [x]: expr }` by `scripts/tpch_rewrite.py`).
3. **(Q13 only)** Implement the `<++` (left override) rewrite (1d) or add it to the grammar.
4. **(Q2 only)** Decide on a syntax for `__rest…` and implement (1e) — out of scope for the MVP demo.

### Tooling for replication

* `scripts/tpch_rewrite.py <N>` — apply Section 1 rewrites to a benchmark Rel file (concatenating `tpch_common_defs.rel`), substitute TPC-H parameters, wrap shorthand defs in `{ … }`, rewrite `(a.b.c)` → `c[b[a]]`, and print the result.
* `scripts/tpch_smoke.sh` — feed each MVP query (full + solo) through `rel2sql_bin -u`, capturing the first parse / translation error.

**Minimum viable subset (first round-trip demo):** Q11, Q17, Q18, Q19, Q21 — currently **blocked at parse time** by the term/expr split above.

**Full 22-query coverage** additionally needs end-to-end benchmark harnessing, Q13 `<++` rewrite, and verification of every verbatim SQL fragment on the target engine (DuckDB/Postgres/etc.).

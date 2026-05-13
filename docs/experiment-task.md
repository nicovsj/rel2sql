# Research & Experimental Roadmap: Rel2SQL Revision

This document outlines the experimental tasks and feasibility checks required for the revision of the Rel2SQL paper, following the rejection from VLDB. The goal is to address reviewer concerns regarding technical evaluation, industrial relevance, and comparative benchmarking.

## Task 1: Proof of Correctness (Rel Query Generation)
**Objective:** Systematically validate the correctness of the Rel2SQL translation logic.

* **Methodology:** Generate a diverse set of Rel queries (ranging from simple selection to complex recursive logic).
* **Validation:** Execute the generated SQL on a target engine (e.g., DuckDB) and compare the results against the original Rel engine (RAI) execution.
* **Goal:** Provide a formal or semi-formal "Success Story" as requested by Reviewer #1 to identify where the design identifies unsafe queries.

## Task 2: TPC-H Round-tripping & Complexity Analysis
**Objective:** Benchmarking against "Gold Standard" manual translations and industrial workloads.

* **Workflow:**
    1.  Start with the existing manual TPC-H translations in Rel (provided by RelationalAI).
    2.  Apply **semantics-preserving source rewrites** so files parse in `RelParser.g4` (strip `@vectorized` / `@inline`, materialise `@@N`, rename `mean`→`average`, replace path sugar with applications, expand Q2 `__rest`, rewrite Q13 `<++`, etc.). Checklist: [`tpch-rel2sql-roundtrip-catalog.md`](tpch-rel2sql-roundtrip-catalog.md).
    3.  Use Rel2SQL (`GetSQLRel` / CLI) to translate the rewritten Rel programs to SQL.
    4.  Compare **Original TPC-H SQL** vs **Rel2SQL-generated SQL** (structure + results on a reference engine).

* **Feasibility verdict (repo state):**
    * A **`BuiltinResolver`** pass plus lowered Rel AST nodes and SQL emission cover the main TPC-H builtins: aggregates (`sum`, `count`, `average`/`avg`/`mean`, `min`, `max`), ordering (`sort`, `reverse_sort`, `bottom` with `ORDER BY` / `LIMIT` on `sql::ast::Select`), dates (`parse_date`, `date_add`, `date_subtract`, `date_year`), decimals (`decimal[…]`, `parse_decimal`), `default_value`, `substring`, typed literals (`^Day`, `^Month`, `^Year`, `^FixedDecimal`), and parenthesized **`like_match(...)`**. All of these are covered by unit tests in `tests/test_translation.cc` (`Builtin*`).
    * **Tooling for the round-trip pre-pass:** `scripts/tpch_rewrite.py <N>` (strip `@vectorized`/`@inline`, materialise `@@N`, rename `mean`→`average`, wrap shorthand defs in `{ … }`, rewrite `(a.b.c)` → `c[b[a]]`) and `scripts/tpch_smoke.sh` (run the MVP subset through the CLI and capture the first error per query).
    * **Minimum viable subset (Q11, Q17, Q18, Q19, Q21) — current state: blocked at parse time.** Smoke-test (`scripts/tpch_smoke.sh`) shows every MVP query fails because `RelParser.g4` separates a scalar **`term`** world (IDs, numerical constants, `+ − × ÷`, parentheses) from an **`expr`** world (literals, products, applications); TPC-H Rel freely mixes them — multiplying two partial applications (`parse_decimal[…] * sum[…]`), comparing partial applications (`sum[l_quantity[ok]] > 300`, `l_suppkey[…] != x`), and equating a variable with a string constant (`x = "SM CASE"`). See [`tpch-rel2sql-roundtrip-catalog.md`](tpch-rel2sql-roundtrip-catalog.md) for the per-query verdict.
    * **Still open / follow-ups (ordered by blocking power):**
        1. Lift `partialAppl` and string/char literals into the `term` non-terminal of `RelParser.g4`; thread the change through `RelASTBuilder` and downstream visitors per `AGENTS.md` (will unblock all five MVP queries).
        2. Add native grammar support for the shorthand defs (`def f[x]: expr`, `def f(x): formula`) so the textual pre-pass becomes optional.
        3. End-to-end runs on all 22 queries; SQL dialect tuning for verbatim fragments; **`like_match[...]`** bracket form if benchmarks use it; Q13 `<++` rewrite; Q2 `__rest…` expansion.
        4. NULL / three-valued logic vs SQL; performance metrics below.

* **Metrics to Measure:**
    * **Join Count:** Compare the number of joins in the generated vs. original queries.
    * **Nesting Depth:** Evaluate the effectiveness of the subquery flattening/CTE inlining optimizer.
    * **Semantic Consistency:** Document any "Lost in Translation" moments, specifically focusing on the NULL semantic gap (3-valued vs. 2-valued logic).
    * **Translation Overhead:** Measure the time taken for fixpoint derivation and Linear Programming (LP) solving to address concerns about NP-hard complexity.

## Task 3: Comparative Analysis with Logica
**Objective:** Demonstrate the advantages of Rel2SQL's safety model and expressiveness over existing transpilers.

* **Methodology:** Compare Rel2SQL against Logica (and potentially PRQL/Malloy) on standard datasets.
* **Evaluation Axes:**
    * **Expressiveness:** Can Rel2SQL handle queries that Logica cannot (e.g., specific recursive patterns or variable bound derivations)?
    * **Safety:** Compare how each tool handles infinite variable ranges.
    * **Output Quality:** Analyze the readability and performance of the generated SQL.

## Feasibility Checklist
* [ ] Verify access to the manual TPC-H Rel implementations.
* [ ] Set up a local environment for Logica benchmarking.
* [ ] Implement a script to automate result comparison between DuckDB (SQL) and RAI (Rel).
* [ ] Quantify the "failure rate" of the translation on complex TPC-H queries to address Reviewer #5's request for coverage analysis.
* [ ]

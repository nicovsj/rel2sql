# Random Rel program generator (tests)

Deterministic, node-budgeted Rel program generation for correctness testing.

## Regenerating programs

Programs are derived at runtime from:

- `seed`
- `program_index`
- `node_budget`
- `edb_map`
- `GeneratorProfile` flags

Nothing is checked into the repo. Re-run generation with different parameters to explore new shapes.

## Running tests

```sh
bazel test --config=default //tests:test_generated_correctness
```

## Dual-path comparison (future)

`RelMatchesSql` compares:

1. Rel engine output (`RelEngine::Run`)
2. `GetSQLRel` → DuckDB (`SqlExecutor::RunProgram`)

Until a Rel backend is available, set `REL2SQL_REL_ENGINE` to a non-empty value only after wiring a real implementation in `rel_engine.cc`. With the stub, the comparison test skips when the variable is unset.

## Profile flags

| Flag | Effect |
|------|--------|
| `allow_recursion` | Canonical transitive-closure `def` on a binary EDB |
| `allow_forall` | `forall((y in Table) \| ...)` (default off; some shapes fail on DuckDB) |
| `allow_aggregates` | `sum[...]`, `max[...]`, etc. |
| `allow_extensional` | Literal tuple unions `{(1,2); (3,4)}` |
| `allow_partial_app` | Partial applications `R[x]` |
| `allow_products` | Product-style joins `(x,y): A(x) and B(y)` |

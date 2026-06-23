# Random Rel program generator (tests)

Deterministic, node-budgeted Rel program generation for correctness testing.

## Regenerating programs

Programs are derived at runtime from:

- `seed`
- `program_index`
- `node_budget`
- `edb_map`
- `GeneratorProfile` flags

Nothing is checked into the repo for live generation tests. Re-run generation with different parameters to explore new shapes.

Fixture values are integers in **1..5** (10 rows per EDB table) so joins hit more often than with a wide random range.

## Running tests

Default CI / local suite (no RAICode required):

```sh
bazel test --config=default //tests:test_generated_correctness
task test
```

Golden corpus (no Julia at test time):

```sh
task test:corpus
# or
bazel test --config=default //tests:test_corpus_correctness
```

## Rel engine server (RAICode)

Persistent Julia worker over a Unix socket — avoids per-query cold start.

### Setup

With [direnv](https://direnv.net/) (recommended):

```sh
direnv allow   # once, in repo root
```

The root [`.envrc`](../../.envrc) sets `JULIA_PROJECT`, `REL2SQL_ENABLE_RAICODE=1`, `REL2SQL_REL_ENGINE_SOCKET`, and `REL2SQL_JULIA` (via `juliaup` when available) if `third_party/raicode` is initialized.

Manual setup:

```sh
git submodule update --init third_party/raicode
export JULIA_PROJECT=$PWD/third_party/raicode
export REL2SQL_ENABLE_RAICODE=1
export REL2SQL_REL_ENGINE_SOCKET=$PWD/.rel_engine.sock
export REL2SQL_JULIA=$(juliaup which 1.10)   # Julia 1.10 required
```

Requires **Julia 1.10** (RAICode `Manifest.toml` is pinned for 1.10.x — not 1.12).

### Server lifecycle

```sh
task rel-engine:start    # foreground: logs in terminal, warm-up timer, Ctrl+C to stop
task rel-engine:ensure   # background if needed (used by corpus:build)
task rel-engine:status
task rel-engine:stop
```

First start typically takes **3-8 minutes** to warm RAICode; cached runs are ~30-60 seconds. Foreground start mirrors output to `.rel_engine.log` via `tee`.

Protocol: NDJSON over `REL2SQL_REL_ENGINE_SOCKET` (default `$REPO_ROOT/.rel_engine.sock`). See [`run_rel_engine_server.jl`](run_rel_engine_server.jl).

`RelEngine::Run` uses the socket when available; otherwise falls back to one-shot [`run_rel_program.jl`](run_rel_program.jl).

On Rel failures, the server returns RAICode compiler **problem reports** (not just `KeyError`) in the JSON `error` field, plus a per-def availability summary when the output relation is missing. Restart `task rel-engine:start` after changing `rel_engine_core.jl`.

The golden corpus uses a **canonical EDB fixture** (`DataFixture::CreateCanonical`) — the same 10-row default tables for every case. The rel-engine server loads EDB once and reuses it across requests (only the generated program is reinstalled each time).

### Run arbitrary Rel

```sh
task rel-engine:start   # recommended (warm server)

# From a file
task rel-engine:run -- query.rl

# Named output relation
task rel-engine:run -- --output myout query.rl

# With EDB fixture (JSON: {"T1": [["1","a"], ["2","b"]], ...})
task rel-engine:run -- --edb fixture.json query.rl

# Stdin
echo 'def output {(1, 2)}' | task rel-engine:run -- -

# Show raw Rel values (tuple vs scalar) before our column normalization
task rel-engine:run -- --raw query.rl

# JSON only (same protocol as the C++ client)
task rel-engine:run -- --json query.rl
```

Direct Julia (same as the script):

```sh
export JULIA_PROJECT=$PWD/third_party/raicode
julia tests/generator/run_rel_snippet.jl program.rl
```

## Golden corpus

Directory: [`corpus/v1/`](corpus/v1/) (local build output; **not** checked into git — regenerate with `task corpus:build`)

| File | Purpose |
|------|---------|
| `manifest.json` | Version, `generator_fingerprint`, shard list, case count |
| `shard_*.jsonl` | One self-contained case per line (program, EDB, Rel-oracle `expected`) |
| `mismatches.jsonl` | Cases where SQL ≠ Rel at build time (known translation gaps) |

### Build / regen

```sh
task rel-engine:ensure   # or task rel-engine:start in another terminal
task corpus:build
task rel-engine:stop   # optional
```

Bump [`corpus_version.h`](corpus_version.h) `kCorpusGeneratorFingerprint` when changing the generator or profile presets, then rebuild the corpus.

### Retry mismatches only

After fixing translation bugs, re-evaluate cases in `mismatches.jsonl` without a full rebuild:

```sh
task rel-engine:ensure
task corpus:retry-mismatches
```

Passing cases are appended to corpus shards; `mismatches.jsonl` and `manifest.json` are updated in place.

The builder grid: **1 seed × 84 program indices × 6 budgets × 1 `full` profile** — 504 candidate slots. Only cases that **translate, execute on Rel, match SQL, and have a novel program text** are written to shards (duplicate program text is skipped so budget sweeps do not inflate coverage).

Case ids look like `s1_i12_b10_pfull`. Multi-def programs use `Gen0`, `Gen1`, … with the **last** def as the oracle output (e.g. `Gen0 → Gen1 → Gen2`, where `Gen2` references only `Gen1`). The dependency graph must be connected with the output def at the top.

### Debug a single case

```sh
bazel run //tests:print_generated_case -- 1 0 6
```

Use seed `1`, program index `0`, and the `full` profile (`FullCorpusProfile()` in `corpus_case_config.cc`) to preview corpus cases.

## RAICode live comparison (manual)

```sh
task test:raicode
```

Small live oracle suite (few generated + static smoke cases). Tagged `manual` / `requires-raicode`; excluded from default CI.

## Profile flags (`full` corpus preset)

The corpus uses a single `full` profile with all of the following enabled:

| Flag | Effect |
|------|--------|
| `allow_recursion` | (disabled in corpus) transitive-closure `def` on a binary EDB |
| `allow_forall` | `forall((y in Table) \| ...)` |
| `allow_aggregates` | `sum[...]`, `max[...]`, etc. |
| `allow_partial_app` | Def-level partial applications `R[x]` |
| `allow_products` | Product-style joins `(x,y): A(x) and B(y)` |
| `allow_extensional` | Literal tuple unions `{(1,2); (3,4)}` |
| `allow_arithmetic` | Terms in atom arguments, e.g. `A(x+1)` |
| `allow_comparisons` | Comparisons in formulas, e.g. `A(x) and x > 3` |
| `allow_negation` | `not` formulas |
| `allow_where` | Expression filters, e.g. `B[x] where A(x)` |
| `allow_binding_exprs` | Binding heads, e.g. `[x in A]: B[x]` |

Def bodies mix intensional rules `(x,y): formula` with expression forms (`sum[A]`, `B[x]`, `[x in A]: …`, etc.).

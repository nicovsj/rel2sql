# AGENTS.md

Guidance for AI agents (and humans) working on the Rel2SQL codebase.

## Project Overview

Rel2SQL translates RelationalAI's [Rel](https://rel.relational.ai/rel) queries to SQL. Implemented in C++ (C++23) with ANTLR4 for parsing, Bazel for building.

**Main pipeline:** `Parse → RelASTBuilder → RelContextBuilder → Translator → (optional) Optimizer`

**Entry points:** `GetSQLRel`, `GetSQLFromFormula`, `GetSQLFromExpr` in `src/api/translate.h`

## Architecture Map

| Directory | Purpose |
|-----------|---------|
| `src/api/` | Public API and translation entry points |
| `src/parser/` | ANTLR lexer/parser wrappers, error handling |
| `src/rel_ast/` | Rel AST nodes, RelContext, builders, visitors |
| `src/sql/` | Rel-to-SQL translation (`translator.h`) |
| `src/sql_ast/` | SQL AST representation (`sql_ast.h`) |
| `src/optimizer/` | SQL optimizers (CTE inliner, flattener, self-join, etc.) |
| `src/preprocessing/` | Analysis visitors (arity, safety, recursion, term_polynomial, etc.) |
| `src/rewriter/` | AST rewriters (binding domain, wildcard, term) |
| `src/validator/` | Validation passes |
| `src/support/` | Utilities and exceptions |
| `src/grammar/` | ANTLR grammar files (`.g4`) |
| `wasm/` | WebAssembly bindings and build config |
| `apps/cli/` | CLI entry point (`main.cc`) |
| `tests/` | Google Test suite; use `tests/test_common.h` for shared helpers |
| `tests/generator/` | Random Rel program generator, golden corpus, RAICode Rel engine client, DuckDB oracle |

## Build & Test Commands

Use `task` (not `bazel` directly) for common operations. Plain `bazel build` / `bazel test` also work on macOS (`.bazelrc` enables the pinned CLT SDK automatically; WASM uses `--config=emcc`, which opts out).

```sh
task build              # Build rel2sql library
task build-shared       # Build shared library
task test               # Run all C++ library tests
task test:lib           # Alias for task test
task test:all           # Run C++ + WASM tests
task test:corpus        # Golden corpus correctness (no Julia)
```

**Run a single test target:**

```sh
bazel test //tests:test_translation
bazel test //tests:test_safety
```

(`--config=default` is optional; `task` still passes it for compatibility.)

**Available test targets:** `test_translation`, `test_sql_ast`, `test_safety`, `test_exceptions`, `test_sql_parser`, `test_rel_ast`, `test_sql_parser_roundtrip`, `test_rewriter`, `test_generated_correctness`, `test_corpus_correctness`

## Generator & correctness tests

Deterministic Rel programs are generated from `(seed, program_index, node_budget, edb_map, GeneratorProfile)`. See [`tests/generator/README.md`](tests/generator/README.md) for full detail.

| Layer | What | When |
|-------|------|------|
| **CI (default)** | `test_generated_correctness` — translate + DuckDB vs inline expectations | Every `task test` |
| **CI (default)** | `test_corpus_correctness` — translate + DuckDB vs checked-in Rel-oracle goldens | `task test:corpus` |
| **Manual** | `test_generated_correctness_raicode` — live Rel vs SQL per query | `task test:raicode` (needs RAICode) |
| **Manual** | `build_corpus` — regen goldens from Rel engine | After generator/profile changes |

**Golden corpus** (`tests/generator/corpus/v1/`): `manifest.json`, `shard_*.jsonl` (passing cases), `mismatches.jsonl` (SQL ≠ Rel at build time — translation bug backlog, not run in CI). Case ids: `s{seed}_i{index}_b{budget}_p{profile}` (e.g. `s1_i12_b10_pfull`). Grid: 1 seed × 84 indices × 6 budgets × `full` profile (504 candidate slots; duplicate program text is skipped at build time). Bump `kCorpusGeneratorFingerprint` in [`tests/generator/corpus_version.h`](tests/generator/corpus_version.h) when changing the generator or profile presets, then `task corpus:build`.

**RAICode Rel engine** (Julia 1.10, `third_party/raicode` submodule):

```sh
task rel-engine:start    # foreground: logs + warm-up timer in terminal (Ctrl+C to stop)
task rel-engine:ensure   # background start if not running (for corpus:build)
task rel-engine:status
task rel-engine:run -- query.rl          # run arbitrary Rel (see --output, --edb, --raw)
task rel-engine:stop
```

`RelEngine::Run` (C++) prefers the socket; falls back to one-shot `run_rel_program.jl`. After editing `rel_engine_core.jl` or server scripts, restart the server.

**Debug binaries** (tagged `manual`; need RAICode for Rel side):

```sh
bazel run //tests:print_generated_case -- 1 0 6              # seed/index/budget
bazel run //tests:repro_mismatch -- s1_i0_b10_pfull        # corpus mismatch case id
bazel run //tests:build_corpus                               # regen corpus (server required)
```

`repro_mismatch` prints Rel program, SQL, fixture, and both result sets. `mismatches.jsonl` reasons: `sql_mismatch` (wrong result/shape) or `sql_exec_error` (invalid SQL). Use `task rel-engine:run -- --raw` to inspect raw Rel tuple vs scalar shape (column count is inferred from row width in `rel_engine_core.jl`).

**Optional live RAICode comparison** (manual, not in default CI): `test_generated_correctness_raicode` — requires `git submodule update --init third_party/raicode` and `REL2SQL_ENABLE_RAICODE=1`.

## Other commands

**Run the CLI:**

```sh
echo 'def output {"Hello"}' | bazel run //:rel2sql_bin
bazel run //:rel2sql_bin -- -f query.rl
bazel run //:rel2sql_bin -- -u -f query.rl   # -u = skip optimizations
```

**IDE setup (clangd):**

```sh
task generate-compile-commands
```

`compile_commands` generation uses a `git_override` on `hedron_compile_commands` in `MODULE.bazel` (fork URL and commit there), not only the upstream Hedron repo.

**Visualize ANTLR parse tree (GUI):**

```sh
task parse-antlr -- test.rl
```

**Debug build:** `bazel build --config debug //...`

## C++ Conventions

- **Namespace:** `rel2sql`
- **AST pattern:** Visitor pattern — nodes implement `DispatchVisit(BaseRelVisitor&, self)`
- **Ownership:** `std::shared_ptr` / `std::unique_ptr`
- **Code style:** clang-format with Google style, 120-char column limit
- **Source locations:** AST nodes carry `antlr4::ParserRuleContext* ctx` for error reporting

## Common Workflows

- **Adding a new Rel construct:** `src/grammar/*.g4` → `RelASTBuilder` → `RelContextBuilder` → `Translator`
- **Adding an optimizer:** Extend `sql::ast::Optimizer` or `base_optimizer` in `src/optimizer/`
- **Adding a preprocessing visitor:** Follow patterns in `src/preprocessing/` (e.g., `arity_visitor`, `safety_inferrer`)

## Testing

- Tests live in `tests/` (e.g. `test_rel_ast.cc`, `test_rewriter.cc`)
- Use `tests/test_common.h` for shared helpers
- Run tests before committing: `task test` or `task test:lib` (alias)
- **CI:** `.github/workflows/test.yml` runs native `bazel test //...`. `.github/workflows/package-consume-test.yml` runs the WASM npm package script and the WASM bindings contract test (see that workflow).
- **Generator tests:** do not commit corpus regen unless intentional; `mismatches.jsonl` entries are known gaps, not failures. Fixing a translation bug → add/run a focused unit test, optionally regen corpus and remove the case from mismatches.

## Pre-commit

Git hooks are defined in `.pre-commit-config.yaml` via [pre-commit-hooks](https://github.com/pre-commit/pre-commit-hooks) and [pocc/pre-commit-hooks](https://github.com/pocc/pre-commit-hooks) (`clang-format` only). `clang-format` must be on your **`PATH`** (e.g. Homebrew LLVM is keg-only until you `export PATH="$(brew --prefix llvm)/bin:$PATH"`).

The repo does **not** pin a LLVM patch version in pre-commit (avoiding mismatches between Homebrew, Xcode/Cursor, and Linux CI). If you need identical formatter output everywhere, align LLVM installs and optionally add `args: [--version=X.Y.Z]` back for the hook.

**Cursor / GUI commits:** The integrated terminal loads [`.vscode/settings.json`](.vscode/settings.json) `terminal.integrated.env.*` so LLVM tools are found when `brew install llvm` is used. Commits triggered from the **Source Control UI** inherit the app environment (not the terminal profile): if hooks report `clang-format` missing, open Cursor from a shell where `PATH` includes LLVM (`cursor .`) or set the same `PATH` in your login environment.

```sh
pip install pre-commit   # or uv tool install pre-commit
pre-commit install
```

`clang-tidy` is not run by pre-commit (it is too slow for every commit); run it manually or in CI. It is most accurate when `compile_commands.json` exists at the repo root (`task generate-compile-commands`). Checks are configured in `.clang-tidy`.

## Key Files

- `src/api/translate.h` — Pipeline and API entry points
- `src/rel_ast/rel_ast.h` — Rel AST node types
- `src/rel_ast/rel_context.h` — Context and relation info
- `src/sql/translator.h` — Rel-to-SQL translation logic
- `tests/generator/rel_program_generator.{h,cc}` — deterministic Rel program generator
- `tests/generator/rel_engine_core.jl` — RAICode execution + result normalization
- `tests/generator/corpus.{h,cc}` — golden corpus loader (`corpus/v1/`)
- `tests/test_corpus_correctness.cc` — CI corpus regression tests

## Git Conventions

Always use gitmojis in commit messages (e.g. `✨ Add feature`, `🐛 Fix bug`, `♻️ Refactor`, `📝 Update docs`).

## Pitfalls to Avoid

- Don't modify `src/grammar/` without updating `RelASTBuilder` and downstream visitors
- Don't bypass `RelContextBuilder` when adding new Rel constructs
- Don't add heavy dependencies without checking WASM compatibility
- When bumping ANTLR, update **both** the Java tool JAR (`ANTLR4_VERSION` / `http_jar` in `MODULE.bazel`) and the C++ runtime (`bazel_dep` `antlr4-cpp-runtime`); they are intentionally allowed to differ today—change them together only when you mean to
- Run `task test` (or `task test:lib`) before committing
- RAICode requires **Julia 1.10** (not 1.12); set `JULIA_PROJECT=third_party/raicode`
- Don't regen the golden corpus without bumping `kCorpusGeneratorFingerprint` in `corpus_version.h`
- macOS native builds need the pinned CLT SDK in `.bazelrc`; WASM (`--config=emcc`) must not inherit macOS `-isysroot`

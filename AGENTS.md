# AGENTS.md

Guidance for AI agents working on the Rel2SQL codebase.

## Project Overview

Rel2SQL translates RelationalAI's [Rel](https://rel.relational.ai/rel) queries to SQL. Implemented in C++ with ANTLR for parsing, Bazel for building.

**Main pipeline:** `Parse → RelASTBuilder → RelContextBuilder → Translator → (optional) Optimizer`

**Entry points:** `GetSQLRel`, `GetSQLFromFormula`, `GetSQLFromExpr` in `src/api/translate.h`

## Architecture Map

| Directory | Purpose |
|-----------|---------|
| `src/api/` | Public API and translation entry points |
| `src/parser/` | ANTLR lexer/parser, error handling |
| `src/rel_ast/` | Rel AST nodes, RelContext, builders, visitors |
| `src/sql/` | Rel-to-SQL translation |
| `src/sql_ast/` | SQL AST representation |
| `src/optimizer/` | SQL optimizers (CTE inliner, flattener, etc.) |
| `src/preprocessing/` | Visitors (arity, safety, recursion, term_polynomial, etc.) |
| `src/rewriter/` | AST rewriters (binding domain, underscore, expression-as-term) |
| `src/validator/` | Validation passes |
| `grammar/` | ANTLR grammar files |
| `wasm/` | WebAssembly build and bindings |

## Build & Test Commands

- **Build:** `task build` (don't use bazel directly)
- **Tests:** `task test` or `task test:lib` for library tests only (don't use bazel directly)
- **Compile commands (clangd):** `task generate-compile-commands`
- **Debug build:** `bazel build --config debug //...`

## C++ Conventions

- **Namespace:** `rel2sql`
- **AST pattern:** Visitor pattern (`RelBaseVisitor`, `Accept()` on nodes)
- **Ownership:** Prefer `std::shared_ptr` / `std::unique_ptr` where appropriate
- **Source locations:** Nodes carry `antlr4::ParserRuleContext* ctx` for error reporting

## Common Workflows

- **Adding a new Rel construct:** Grammar → `RelASTBuilder` → `RelContextBuilder` → `Translator`
- **Adding an optimizer:** Extend `sql::ast::Optimizer` or `base_optimizer`
- **Adding a visitor:** Follow patterns in `src/preprocessing/` (e.g. `arity_visitor`, `safety_inferrer`)

## Testing

- Tests live in `tests/` (e.g. `test_rel_ast.cc`, `test_rewriter.cc`)
- Use `tests/test_common.h` for shared helpers
- Run tests before committing: `task test:lib`

## Pitfalls to Avoid

- Don't modify grammar without updating `RelASTBuilder` and downstream visitors
- Don't bypass `RelContextBuilder` when adding new Rel constructs
- Don't add heavy dependencies without checking WASM compatibility

## Key Files to Read First

- `src/api/translate.h` — Pipeline and API
- `src/rel_ast/rel_ast.h` — Rel AST node types
- `src/rel_ast/rel_context.h` — Context and relation info
- `src/sql/translator.h` — Rel-to-SQL translation

# Rel2SQL

A tool that translates RelationalAI's [Rel](https://rel.relational.ai/rel) queries to SQL. Implemented in C++ with ANTLR for parsing Rel queries.

For architecture, conventions, pre-commit setup, and agent-oriented notes, see [AGENTS.md](AGENTS.md).

## Usage

The CLI reads Rel from stdin or a file and prints the translated SQL:

```sh
# From stdin
echo 'def output {"Hello World!"}' | bazel run //:rel2sql_bin

# From file
bazel run //:rel2sql_bin -- -f query.rl

# Unoptimized translation (skip optimizations)
bazel run //:rel2sql_bin -- -u -f query.rl
```

## Table of Contents

- [Rel2SQL](#rel2sql)
  - [Usage](#usage)
  - [Table of Contents](#table-of-contents)
  - [Prerequisites](#prerequisites)
  - [New machine checklist](#new-machine-checklist)
  - [Building the Project](#building-the-project)
  - [Development](#development)
    - [Generating `compile_commands.json`](#generating-compile_commandsjson)
    - [Testing](#testing)
    - [Formatting and pre-commit](#formatting-and-pre-commit)
  - [WebAssembly (WASM) Support](#webassembly-wasm-support)
    - [Building WASM](#building-wasm)
    - [Testing WASM](#testing-wasm)
    - [WASM Documentation](#wasm-documentation)
  - [Troubleshooting](#troubleshooting)
  - [Extra](#extra)
  - [Available Tasks](#available-tasks)

## Prerequisites

This project uses Bazel as its build system and Task as its task runner.

- **[Bazelisk](https://github.com/bazelbuild/bazelisk)** (recommended) — installs and runs the Bazel version from [`.bazelversion`](.bazelversion) (currently 8.x). Do not rely on a generic “Bazel 7+” install; match the pinned version.
- **[Task](https://taskfile.dev/)** (3.0.0 or later)
- **C++ compiler** with C++23 support (e.g. `clang++` or `g++`)
- **[JDK](https://adoptium.net/)** 11 or later (ANTLR code generation)
- **For WASM workflows:** [Node.js](https://nodejs.org/) and **npm** (package tests and some `task test:wasm:*` steps); **Python 3** for the local HTTP server used by `task test:wasm:browser`
- **Optional:** `antlr4-parse` on your `PATH` for `task parse-antlr` (ANTLR 4 testrig-style GUI)

## New machine checklist

1. Install Bazelisk, Task, JDK, and a C++ toolchain.
2. `task build` then `task test`.
3. `pre-commit install` if you will commit (see [AGENTS.md](AGENTS.md)); ensure `clang-format` is on your `PATH`.
4. `task generate-compile-commands` if you use clangd (generates ignored `compile_commands.json` at repo root).
5. For WASM: install Node/npm (and Python 3 for browser serving); run `task test:all` or the WASM section tasks as needed.

## Building the Project

To build the rel2sql **library**:

```sh
task build
```

To build the **shared** library:

```sh
task build-shared
```

The **CLI** binary is a separate target; run it with:

```sh
bazel run //:rel2sql_bin -- -f query.rl
```

## Development

### Generating `compile_commands.json`

For clangd and similar tools:

```sh
task generate-compile-commands
```

This uses the [Hedron compile-commands extractor](https://github.com/hedronvision/bazel-compile-commands-extractor) pattern; this repo pins a **`git_override`** fork in [`MODULE.bazel`](MODULE.bazel) (`mikael-s-persson/bazel-compile-commands-extractor`). See that file for the exact commit.

WASM/Emscripten IntelliSense (optional):

```sh
task generate-compile-commands-with-wasm
```

### Testing

- **`task test`** (same as **`task test:lib`**) — runs **`bazel test //...`** with `--config=default`. This covers **C++ tests** under `tests/`. WASM build targets are tagged `manual` and are **not** included here.
- **`task test:all`** — runs native tests, then **`task test:wasm`** (Node-based WASM checks and browser server; see Taskfile).

CI on pull requests: [`.github/workflows/test.yml`](.github/workflows/test.yml) runs native Bazel tests; [`.github/workflows/package-consume-test.yml`](.github/workflows/package-consume-test.yml) builds WASM via `wasm/tests/npm_package_test.mjs` and runs the bindings contract test.

### Formatting and pre-commit

Hooks use `clang-format` (see `.pre-commit-config.yaml`). Setup and editor PATH notes are in [AGENTS.md](AGENTS.md).

## WebAssembly (WASM) Support

Rel2SQL can be compiled to WebAssembly for use in web browsers and Node.js.

### Building WASM

```sh
task build-wasm
```

Individual targets:

```sh
task build-wasm-node   # Node.js only
task build-wasm-browser  # Browser only
```

### Testing WASM

- **`task test:wasm`** — bindings contract, npm package flow, then a local server for browser HTML (see Taskfile).
- **`task test:wasm:bindings`** — `node wasm/tests/bindings_test.mjs` (requires `bazel build //wasm:rel2sql_wasm_node --config=emcc` first, or run after a WASM build).
- **`task test:wasm:npm`** — npm package build/pack/consumption test.
- **`task test:wasm:browser`** — copies artifacts and runs `python3 -m http.server 8080`; open `http://localhost:8080/wasm/tests/browser/test_browser.html`.

To run **native + WASM** tasks in one go:

```sh
task test:all
```

### WASM Documentation

See [`wasm/TESTING.md`](wasm/TESTING.md).

## Troubleshooting

- **macOS SDK / header errors:** The shared [`.bazelrc`](.bazelrc) pins `-isysroot` to Command Line Tools **`MacOSX14.5.sdk`** for consistent builds with the pinned toolchain. If that directory is missing, install CLT or add a **user** `.bazelrc` / `user.bazelrc` line overriding `build:macos` — e.g. `.../SDKs/MacOSX.sdk` or the path from `xcrun --sdk macosx --show-sdk-path`. (Using a much newer SDK than the pin can surface different Clang/module behavior; prefer matching the pin when possible.)
- **Debugging under Bazel on macOS:** [bazelbuild/bazel#6327](https://github.com/bazelbuild/bazel/issues/6327).
- **LLDB and Bazel:** Use `bazel info execution_root` and set LLDB’s platform working directory to that path if source maps / paths misbehave; see [`.lldbinit.example`](.lldbinit.example).

## Extra

- You can use the following command to show a GUI with the AST (requires `antlr4-parse`):
  ```sh
  task parse-antlr -- test.rl
  ```

## Available Tasks

```sh
task
```

Lists all tasks from [`Taskfile.yml`](Taskfile.yml).
